/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Raft consensus server - node.go
 */
package raft

import (
	"context"
	"fmt"
	"log"
	"math/rand"
	"sync"
	"sync/atomic"
	"time"
)

// Node Raft节点
type Node struct {
	// 基本信息
	id     NodeID
	config *Config
	logger *log.Logger

	// 组件
	transport    Transport
	storage      Storage
	stateMachine StateMachine

	// 持久化状态
	currentTerm atomic.Uint64 // 当前任期
	votedFor    atomic.Value  // 当前任期投票给的候选人

	// 易失状态
	mu          sync.RWMutex
	state       NodeState // 当前状态
	leader      NodeID    // 当前领导者
	commitIndex LogIndex  // 已知已提交的最高日志索引
	lastApplied LogIndex  // 已应用到状态机的最高日志索引

	// 领导者状态（选举后重新初始化）
	nextIndex  map[NodeID]LogIndex // 对于每个服务器，要发送的下一个日志条目索引
	matchIndex map[NodeID]LogIndex // 对于每个服务器，已知已复制的最高日志索引

	// 时间相关
	lastHeartbeat   time.Time    // 最后收到心跳的时间
	electionTimer   *time.Timer  // 选举超时定时器
	heartbeatTicker *time.Ticker // 心跳定时器

	// 控制
	ctx        context.Context    // 上下文
	cancel     context.CancelFunc // 取消函数
	shutdownCh chan struct{}      // 关闭信号
	wg         sync.WaitGroup     // 等待组

	// 事件
	eventListeners []EventListener

	// 指标
	metrics atomic.Value // *Metrics
}

// NewNode 创建新的Raft节点
func NewNode(config *Config, transport Transport, storage Storage, stateMachine StateMachine) (*Node, error) {
	if config == nil {
		return nil, fmt.Errorf("配置不能为空")
	}

	if transport == nil {
		return nil, fmt.Errorf("传输层不能为空")
	}

	if storage == nil {
		return nil, fmt.Errorf("存储层不能为空")
	}

	if stateMachine == nil {
		return nil, fmt.Errorf("状态机不能为空")
	}

	ctx, cancel := context.WithCancel(context.Background())

	node := &Node{
		id:           config.NodeID,
		config:       config,
		logger:       log.New(log.Writer(), fmt.Sprintf("[raft-%s] ", config.NodeID), log.LstdFlags),
		transport:    transport,
		storage:      storage,
		stateMachine: stateMachine,
		state:        Follower,
		nextIndex:    make(map[NodeID]LogIndex),
		matchIndex:   make(map[NodeID]LogIndex),
		ctx:          ctx,
		cancel:       cancel,
		shutdownCh:   make(chan struct{}),
	}

	// 从存储恢复状态
	if err := node.restoreState(); err != nil {
		cancel()
		return nil, fmt.Errorf("恢复状态失败: %w", err)
	}

	// 初始化定时器
	node.resetElectionTimer()

	// 初始化指标
	node.updateMetrics()

	return node, nil
}

// Start 启动节点
func (n *Node) Start() error {
	n.logger.Printf("启动Raft节点 %s", n.id)

	// 启动传输层
	if err := n.transport.Start(); err != nil {
		return fmt.Errorf("启动传输层失败: %w", err)
	}

	// 启动主循环
	n.wg.Add(1)
	go n.run()

	return nil
}

// Stop 停止节点
func (n *Node) Stop() error {
	n.logger.Printf("停止Raft节点 %s", n.id)

	// 发送关闭信号
	close(n.shutdownCh)

	// 取消上下文
	n.cancel()

	// 等待goroutine结束
	n.wg.Wait()

	// 停止定时器
	if n.electionTimer != nil {
		n.electionTimer.Stop()
	}
	if n.heartbeatTicker != nil {
		n.heartbeatTicker.Stop()
	}

	// 停止传输层
	if err := n.transport.Stop(); err != nil {
		n.logger.Printf("停止传输层失败: %v", err)
	}

	// 关闭存储
	if err := n.storage.Close(); err != nil {
		n.logger.Printf("关闭存储失败: %v", err)
	}

	return nil
}

// run 主循环
func (n *Node) run() {
	defer n.wg.Done()

	for {
		select {
		case <-n.shutdownCh:
			return
		case <-n.ctx.Done():
			return
		case <-n.electionTimer.C:
			n.handleElectionTimeout()
		default:
			// 检查心跳定时器是否存在
			if n.heartbeatTicker != nil {
				select {
				case <-n.shutdownCh:
					return
				case <-n.ctx.Done():
					return
				case <-n.electionTimer.C:
					n.handleElectionTimeout()
				case <-n.heartbeatTicker.C:
					n.sendHeartbeats()
				case <-time.After(time.Millisecond * 10):
					// 避免阻塞
				}
			} else {
				// 如果没有心跳定时器，短暂休眠避免CPU占用过高
				time.Sleep(time.Millisecond * 10)
			}
		}
	}
}

// restoreState 从存储恢复状态
func (n *Node) restoreState() error {
	// 恢复当前任期
	term, err := n.storage.GetCurrentTerm()
	if err != nil {
		return fmt.Errorf("获取当前任期失败: %w", err)
	}
	n.currentTerm.Store(uint64(term))

	// 恢复投票状态
	votedFor, err := n.storage.GetVotedFor()
	if err != nil {
		return fmt.Errorf("获取投票状态失败: %w", err)
	}
	n.votedFor.Store(votedFor)

	// 初始化commitIndex和lastApplied
	lastLogIndex := n.storage.GetLastLogIndex()
	n.commitIndex = lastLogIndex
	n.lastApplied = lastLogIndex

	return nil
}

// getCurrentTerm 获取当前任期
func (n *Node) getCurrentTerm() Term {
	return Term(n.currentTerm.Load())
}

// setCurrentTerm 设置当前任期
func (n *Node) setCurrentTerm(term Term) error {
	n.currentTerm.Store(uint64(term))
	if err := n.storage.SaveCurrentTerm(term); err != nil {
		return fmt.Errorf("保存当前任期失败: %w", err)
	}
	return nil
}

// getVotedFor 获取投票对象
func (n *Node) getVotedFor() NodeID {
	if v := n.votedFor.Load(); v != nil {
		return v.(NodeID)
	}
	return ""
}

// setVotedFor 设置投票对象
func (n *Node) setVotedFor(candidateID NodeID) error {
	n.votedFor.Store(candidateID)
	if err := n.storage.SaveVotedFor(candidateID); err != nil {
		return fmt.Errorf("保存投票状态失败: %w", err)
	}
	return nil
}

// becomeFollower 转换为跟随者
func (n *Node) becomeFollower(term Term, leader NodeID) {
	n.mu.Lock()
	defer n.mu.Unlock()

	oldState := n.state
	n.state = Follower
	n.leader = leader

	if term > n.getCurrentTerm() {
		if err := n.setCurrentTerm(term); err != nil {
			n.logger.Printf("设置任期失败: %v", err)
		}
		if err := n.setVotedFor(""); err != nil {
			n.logger.Printf("清除投票状态失败: %v", err)
		}
	}

	n.resetElectionTimer()

	if n.heartbeatTicker != nil {
		n.heartbeatTicker.Stop()
		n.heartbeatTicker = nil
	}

	n.logger.Printf("转换为跟随者，任期: %d，领导者: %s", term, leader)

	// 触发状态变更事件
	n.notifyStateChange(oldState, n.state, term)

	n.updateMetrics()
}

// becomeCandidate 转换为候选人
func (n *Node) becomeCandidate() {
	n.mu.Lock()
	defer n.mu.Unlock()

	oldState := n.state
	n.state = Candidate
	n.leader = ""

	// 增加任期
	newTerm := n.getCurrentTerm() + 1
	n.logger.Printf("准备设置新任期: %d", newTerm)
	if err := n.setCurrentTerm(newTerm); err != nil {
		n.logger.Printf("设置任期失败: %v", err)
		return
	}
	n.logger.Printf("成功设置新任期: %d", newTerm)

	// 投票给自己
	n.logger.Printf("准备投票给自己: %s", n.id)
	if err := n.setVotedFor(n.id); err != nil {
		n.logger.Printf("投票给自己失败: %v", err)
		return
	}
	n.logger.Printf("成功投票给自己: %s", n.id)

	n.resetElectionTimer()

	n.logger.Printf("转换为候选人，任期: %d", newTerm)

	n.logger.Printf("DEBUG: 准备触发状态变更事件")
	// 触发状态变更事件
	n.notifyStateChange(oldState, n.state, newTerm)

	n.logger.Printf("DEBUG: 准备更新指标")
	// 手动更新指标，避免死锁
	metrics := &Metrics{
		CurrentTerm:  n.getCurrentTerm(),
		State:        n.state,
		Leader:       n.leader,
		LastLogIndex: n.storage.GetLastLogIndex(),
		CommitIndex:  n.commitIndex,
		LastApplied:  n.lastApplied,
	}
	n.metrics.Store(metrics)

	n.logger.Printf("DEBUG: 准备开始选举，启动 startElection goroutine")
	// 开始选举
	go func() {
		n.logger.Printf("DEBUG: startElection goroutine 已启动")
		n.startElection()
		n.logger.Printf("DEBUG: startElection goroutine 已完成")
	}()

	n.logger.Printf("DEBUG: becomeCandidate 函数即将结束")
}

// becomeLeader 转换为领导者
func (n *Node) becomeLeader() {
	n.mu.Lock()

	oldState := n.state
	oldLeader := n.leader
	n.state = Leader
	n.leader = n.id

	// 初始化领导者状态
	lastLogIndex := n.storage.GetLastLogIndex()
	for _, server := range n.config.Servers {
		if server.ID != n.id {
			n.nextIndex[server.ID] = lastLogIndex + 1
			n.matchIndex[server.ID] = 0
		}
	}

	// 停止选举定时器
	if n.electionTimer != nil {
		n.electionTimer.Stop()
	}

	// 启动心跳定时器
	n.heartbeatTicker = time.NewTicker(n.config.HeartbeatInterval)

	currentTerm := n.getCurrentTerm()
	n.logger.Printf("成为领导者，任期: %d", currentTerm)

	// 手动更新指标，避免在锁内调用可能阻塞的方法
	metrics := &Metrics{
		CurrentTerm:  currentTerm,
		State:        n.state,
		Leader:       n.leader,
		LastLogIndex: lastLogIndex,
		CommitIndex:  n.commitIndex,
		LastApplied:  n.lastApplied,
	}
	n.metrics.Store(metrics)

	// 释放锁
	n.mu.Unlock()

	// 在锁外触发事件和发送心跳
	n.notifyStateChange(oldState, Leader, currentTerm)
	n.notifyLeaderChange(oldLeader, n.id, currentTerm)

	// 立即发送心跳
	go n.sendHeartbeats()
}

// resetElectionTimer 重置选举定时器
func (n *Node) resetElectionTimer() {
	if n.electionTimer != nil {
		n.electionTimer.Stop()
	}

	// 随机化选举超时时间（150%-300%）
	timeout := n.config.ElectionTimeout + time.Duration(rand.Int63n(int64(n.config.ElectionTimeout)))
	n.electionTimer = time.NewTimer(timeout)
	n.lastHeartbeat = time.Now()
}

// handleElectionTimeout 处理选举超时
func (n *Node) handleElectionTimeout() {
	n.mu.RLock()
	state := n.state
	n.mu.RUnlock()

	if state != Leader {
		n.logger.Printf("选举超时，开始新的选举")
		n.becomeCandidate()
	}
}

// updateMetrics 更新指标
func (n *Node) updateMetrics() {
	n.mu.RLock()
	defer n.mu.RUnlock()

	metrics := &Metrics{
		CurrentTerm:  n.getCurrentTerm(),
		State:        n.state,
		Leader:       n.leader,
		LastLogIndex: n.storage.GetLastLogIndex(),
		CommitIndex:  n.commitIndex,
		LastApplied:  n.lastApplied,
	}

	n.metrics.Store(metrics)
}

// GetMetrics 获取指标
func (n *Node) GetMetrics() *Metrics {
	if m := n.metrics.Load(); m != nil {
		return m.(*Metrics)
	}
	return &Metrics{}
}

// AddEventListener 添加事件监听器
func (n *Node) AddEventListener(listener EventListener) {
	n.mu.Lock()
	defer n.mu.Unlock()
	n.eventListeners = append(n.eventListeners, listener)
}

// notifyStateChange 通知状态变更
func (n *Node) notifyStateChange(oldState, newState NodeState, term Term) {
	event := &StateChangeEvent{
		OldState: oldState,
		NewState: newState,
		Term:     term,
	}

	for _, listener := range n.eventListeners {
		go listener.OnEvent(event)
	}
}

// notifyLeaderChange 通知领导者变更
func (n *Node) notifyLeaderChange(oldLeader, newLeader NodeID, term Term) {
	event := &LeaderChangeEvent{
		OldLeader: oldLeader,
		NewLeader: newLeader,
		Term:      term,
	}

	for _, listener := range n.eventListeners {
		go listener.OnEvent(event)
	}
}

// IsLeader 是否为领导者
func (n *Node) IsLeader() bool {
	n.mu.RLock()
	defer n.mu.RUnlock()
	return n.state == Leader
}

// GetLeader 获取当前领导者
func (n *Node) GetLeader() NodeID {
	n.mu.RLock()
	defer n.mu.RUnlock()
	return n.leader
}

// GetState 获取当前状态
func (n *Node) GetState() NodeState {
	n.mu.RLock()
	defer n.mu.RUnlock()
	return n.state
}

// GetID 获取节点ID
func (n *Node) GetID() NodeID {
	return n.id
}
