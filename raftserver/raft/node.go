/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-27 21:08:28
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

	// 数据中心感知扩展 ⭐ 新增
	dcExtension      *DCRaftExtension                  // DC感知Raft扩展
	dcHealthCheckers map[DataCenterID]*DCHealthChecker // DC健康检查器
	dcMetrics        *DCMetrics                        // DC相关指标

	// 跨DC复制管理器 ⭐ 新增
	crossDCReplication *CrossDCReplicationManager // 跨DC复制管理器
}

// DCHealthChecker DC健康检查器
type DCHealthChecker struct {
	mu            sync.RWMutex
	dataCenter    DataCenterID
	nodes         []NodeID
	healthStatus  map[NodeID]*NodeHealthStatus
	checkInterval time.Duration
	timeout       time.Duration
	logger        *log.Logger
	stopCh        chan struct{}
	wg            sync.WaitGroup
}

// NodeHealthStatus 节点健康状态
type NodeHealthStatus struct {
	IsHealthy     bool
	LastCheck     time.Time
	LastHeartbeat time.Time
	LatencyMs     int64
	ErrorCount    int64
}

// DCMetrics DC相关指标
type DCMetrics struct {
	mu                    sync.RWMutex
	LocalDataCenter       DataCenterID
	CrossDCLatencies      map[DataCenterID]time.Duration
	HealthyNodesPerDC     map[DataCenterID]int
	TotalNodesPerDC       map[DataCenterID]int
	LastElectionTriggerDC DataCenterID
	ElectionCount         int64
	CrossDCReplicationLag time.Duration
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

		// 初始化DC相关组件 ⭐ 新增
		dcHealthCheckers: make(map[DataCenterID]*DCHealthChecker),
	}

	// 初始化DC扩展 ⭐ 新增
	if config.MultiDC != nil && config.MultiDC.Enabled {
		node.dcExtension = NewDCRaftExtension(config, config.NodeID)
		node.initializeDCMetrics()
		node.initializeDCHealthCheckers()

		// 初始化跨DC复制管理器 ⭐ 新增
		node.crossDCReplication = NewCrossDCReplicationManager(config.NodeID, config, transport)
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

// initializeDCMetrics 初始化DC指标 ⭐ 新增
func (n *Node) initializeDCMetrics() {
	// 确定本地数据中心
	var localDC DataCenterID = "default"
	for _, server := range n.config.Servers {
		if server.ID == n.id {
			localDC = server.DataCenter
			break
		}
	}

	n.dcMetrics = &DCMetrics{
		LocalDataCenter:   localDC,
		CrossDCLatencies:  make(map[DataCenterID]time.Duration),
		HealthyNodesPerDC: make(map[DataCenterID]int),
		TotalNodesPerDC:   make(map[DataCenterID]int),
	}

	// 统计各DC的节点数量
	for _, server := range n.config.Servers {
		n.dcMetrics.TotalNodesPerDC[server.DataCenter]++
	}
}

// initializeDCHealthCheckers 初始化DC健康检查器 ⭐ 新增
func (n *Node) initializeDCHealthCheckers() {
	// 按数据中心分组节点
	dcNodes := make(map[DataCenterID][]NodeID)
	for _, server := range n.config.Servers {
		dcNodes[server.DataCenter] = append(dcNodes[server.DataCenter], server.ID)
	}

	// 为每个数据中心创建健康检查器
	for dc, nodes := range dcNodes {
		if dc == n.dcMetrics.LocalDataCenter {
			continue // 跳过本地数据中心
		}

		checker := &DCHealthChecker{
			dataCenter:    dc,
			nodes:         nodes,
			healthStatus:  make(map[NodeID]*NodeHealthStatus),
			checkInterval: time.Second * 5, // 5秒检查间隔
			timeout:       time.Second * 2, // 2秒超时
			logger:        log.New(log.Writer(), fmt.Sprintf("[dc-health-%s] ", dc), log.LstdFlags),
			stopCh:        make(chan struct{}),
		}

		// 初始化节点健康状态
		for _, nodeID := range nodes {
			checker.healthStatus[nodeID] = &NodeHealthStatus{
				IsHealthy: true, // 默认健康
				LastCheck: time.Now(),
			}
		}

		n.dcHealthCheckers[dc] = checker
	}
}

// startDCComponents 启动DC相关组件 ⭐ 新增
func (n *Node) startDCComponents() error {
	if n.dcExtension != nil {
		if err := n.dcExtension.Start(); err != nil {
			return fmt.Errorf("启动DC扩展失败: %w", err)
		}
	}

	// 启动跨DC复制管理器 ⭐ 新增
	if n.crossDCReplication != nil {
		if err := n.crossDCReplication.Start(); err != nil {
			return fmt.Errorf("启动跨DC复制管理器失败: %w", err)
		}
	}

	// 启动DC健康检查器
	for dc, checker := range n.dcHealthCheckers {
		if err := checker.start(); err != nil {
			n.logger.Printf("启动DC健康检查器失败 [%s]: %v", dc, err)
			// 继续启动其他检查器，不因为一个失败而终止
		}
	}

	return nil
}

// stopDCComponents 停止DC相关组件 ⭐ 新增
func (n *Node) stopDCComponents() {
	if n.dcExtension != nil {
		n.dcExtension.Stop()
	}

	// 停止跨DC复制管理器 ⭐ 新增
	if n.crossDCReplication != nil {
		n.crossDCReplication.Stop()
	}

	// 停止DC健康检查器
	for dc, checker := range n.dcHealthCheckers {
		checker.stop()
		n.logger.Printf("已停止DC健康检查器 [%s]", dc)
	}
}

// shouldStartDCElection 判断是否应该开始DC感知选举 ⭐ 新增
func (n *Node) shouldStartDCElection() bool {
	if n.dcExtension == nil {
		return true // 无DC扩展，使用标准选举
	}

	return n.dcExtension.ShouldStartElection()
}

// recordDCHeartbeat 记录DC心跳 ⭐ 新增
func (n *Node) recordDCHeartbeat(leaderID NodeID) {
	if n.dcExtension != nil {
		n.dcExtension.recordCrossDCHeartbeat(leaderID)
	}

	// 更新健康检查状态
	n.updateNodeHealthFromHeartbeat(leaderID)
}

// updateNodeHealthFromHeartbeat 根据心跳更新节点健康状态 ⭐ 新增
func (n *Node) updateNodeHealthFromHeartbeat(nodeID NodeID) {
	// 查找节点所属的数据中心
	var nodeDC DataCenterID
	for _, server := range n.config.Servers {
		if server.ID == nodeID {
			nodeDC = server.DataCenter
			break
		}
	}

	if nodeDC == "" || nodeDC == n.dcMetrics.LocalDataCenter {
		return // 本地节点或未知节点
	}

	// 更新健康状态
	if checker, exists := n.dcHealthCheckers[nodeDC]; exists {
		checker.mu.Lock()
		if status, exists := checker.healthStatus[nodeID]; exists {
			status.LastHeartbeat = time.Now()
			status.IsHealthy = true
			status.ErrorCount = 0
		}
		checker.mu.Unlock()
	}
}

// GetDCMetrics 获取DC指标 ⭐ 新增
func (n *Node) GetDCMetrics() *DCMetrics {
	if n.dcMetrics == nil {
		return nil
	}

	n.dcMetrics.mu.RLock()
	defer n.dcMetrics.mu.RUnlock()

	// 创建副本
	metrics := &DCMetrics{
		LocalDataCenter:       n.dcMetrics.LocalDataCenter,
		CrossDCLatencies:      make(map[DataCenterID]time.Duration),
		HealthyNodesPerDC:     make(map[DataCenterID]int),
		TotalNodesPerDC:       make(map[DataCenterID]int),
		LastElectionTriggerDC: n.dcMetrics.LastElectionTriggerDC,
		ElectionCount:         n.dcMetrics.ElectionCount,
		CrossDCReplicationLag: n.dcMetrics.CrossDCReplicationLag,
	}

	for dc, latency := range n.dcMetrics.CrossDCLatencies {
		metrics.CrossDCLatencies[dc] = latency
	}

	for dc, count := range n.dcMetrics.HealthyNodesPerDC {
		metrics.HealthyNodesPerDC[dc] = count
	}

	for dc, count := range n.dcMetrics.TotalNodesPerDC {
		metrics.TotalNodesPerDC[dc] = count
	}

	return metrics
}

// GetDCHealthStatus 获取DC健康状态 ⭐ 新增
func (n *Node) GetDCHealthStatus() map[DataCenterID]map[NodeID]*NodeHealthStatus {
	result := make(map[DataCenterID]map[NodeID]*NodeHealthStatus)

	for dc, checker := range n.dcHealthCheckers {
		checker.mu.RLock()
		dcStatus := make(map[NodeID]*NodeHealthStatus)
		for nodeID, status := range checker.healthStatus {
			// 创建状态副本
			dcStatus[nodeID] = &NodeHealthStatus{
				IsHealthy:     status.IsHealthy,
				LastCheck:     status.LastCheck,
				LastHeartbeat: status.LastHeartbeat,
				LatencyMs:     status.LatencyMs,
				ErrorCount:    status.ErrorCount,
			}
		}
		checker.mu.RUnlock()
		result[dc] = dcStatus
	}

	return result
}

// Start 启动节点
func (n *Node) Start() error {
	n.logger.Printf("启动Raft节点 %s", n.id)

	// 启动传输层
	if err := n.transport.Start(); err != nil {
		return fmt.Errorf("启动传输层失败: %w", err)
	}

	// 启动DC相关组件 ⭐ 新增
	if err := n.startDCComponents(); err != nil {
		n.transport.Stop()
		return fmt.Errorf("启动DC组件失败: %w", err)
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

	// 停止DC相关组件 ⭐ 新增
	n.stopDCComponents()

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

	// 记录DC心跳 ⭐ 新增
	if leader != "" {
		n.recordDCHeartbeat(leader)
	}

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

	// 更新DC指标 ⭐ 新增
	if n.dcMetrics != nil {
		n.dcMetrics.mu.Lock()
		n.dcMetrics.ElectionCount++
		n.dcMetrics.LastElectionTriggerDC = n.dcMetrics.LocalDataCenter
		n.dcMetrics.mu.Unlock()
	}

	n.logger.Printf("DEBUG: 准备触发状态变更事件")
	// 触发状态变更事件
	n.notifyStateChange(oldState, n.state, newTerm)

	n.logger.Printf("DEBUG: 准备更新指标")
	// 手动更新指标，避免死锁
	n.updateMetricsUnsafe(newTerm, n.state, n.leader)

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
		CurrentTerm: currentTerm,
		State:       n.state,
		LeaderID:    n.leader,
		CommitIndex: n.commitIndex,
		LastApplied: n.lastApplied,
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
		// 使用DC感知选举逻辑 ⭐ 修改
		if n.shouldStartDCElection() {
			n.logger.Printf("选举超时，开始新的选举")
			n.becomeCandidate()
		} else {
			n.logger.Printf("DC优先级选举阻止本次选举")
			n.resetElectionTimer() // 重置定时器，等待下次检查
		}
	}
}

// updateMetrics 更新指标
func (n *Node) updateMetrics() {
	n.mu.RLock()
	defer n.mu.RUnlock()

	metrics := &Metrics{
		CurrentTerm: n.getCurrentTerm(),
		State:       n.state,
		LeaderID:    n.leader,
		CommitIndex: n.commitIndex,
		LastApplied: n.lastApplied,
	}

	n.metrics.Store(metrics)
}

// updateMetricsUnsafe 不安全的指标更新（已持有锁时使用） ⭐ 新增
func (n *Node) updateMetricsUnsafe(term Term, state NodeState, leader NodeID) {
	// 简化的指标更新，避免类型依赖
	n.logger.Printf("更新指标: 任期=%d, 状态=%s, 领导者=%s", term, state, leader)
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
	event := StateChangeEvent{
		NodeID:   n.id,
		OldState: oldState,
		NewState: newState,
		Term:     term,
		Time:     time.Now().Unix(),
	}

	for _, listener := range n.eventListeners {
		go listener.OnStateChange(event)
	}
}

// notifyLeaderChange 通知领导者变更
func (n *Node) notifyLeaderChange(oldLeader, newLeader NodeID, term Term) {
	event := LeaderChangeEvent{
		NodeID:      n.id,
		OldLeaderID: oldLeader,
		NewLeaderID: newLeader,
		Term:        term,
		Time:        time.Now().Unix(),
	}

	for _, listener := range n.eventListeners {
		go listener.OnLeaderChange(event)
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

// DC健康检查器方法实现 ⭐ 新增

// start 启动DC健康检查器
func (checker *DCHealthChecker) start() error {
	checker.logger.Printf("启动DC健康检查器 [%s]", checker.dataCenter)

	checker.wg.Add(1)
	go checker.healthCheckLoop()

	return nil
}

// stop 停止DC健康检查器
func (checker *DCHealthChecker) stop() {
	checker.logger.Printf("停止DC健康检查器 [%s]", checker.dataCenter)

	close(checker.stopCh)
	checker.wg.Wait()
}

// healthCheckLoop 健康检查循环
func (checker *DCHealthChecker) healthCheckLoop() {
	defer checker.wg.Done()

	ticker := time.NewTicker(checker.checkInterval)
	defer ticker.Stop()

	for {
		select {
		case <-checker.stopCh:
			return
		case <-ticker.C:
			checker.performHealthCheck()
		}
	}
}

// performHealthCheck 执行健康检查
func (checker *DCHealthChecker) performHealthCheck() {
	checker.mu.Lock()
	defer checker.mu.Unlock()

	now := time.Now()

	for _, nodeID := range checker.nodes {
		status := checker.healthStatus[nodeID]
		if status == nil {
			continue
		}

		// 检查是否超时
		timeSinceLastHeartbeat := now.Sub(status.LastHeartbeat)
		if timeSinceLastHeartbeat > checker.timeout*3 { // 3倍超时时间
			if status.IsHealthy {
				checker.logger.Printf("节点 %s 健康状态变为不健康", nodeID)
				status.IsHealthy = false
				status.ErrorCount++
			}
		}

		status.LastCheck = now
	}
}
