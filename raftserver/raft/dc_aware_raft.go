/*
* @Author: Lzww0608
* @Date: 2025-6-27 21:32:06
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-27 21:32:10
* @Description: ConcordKV Raft consensus server - dc_aware_raft.go
 */

package raft

import (
	"fmt"
	"log"
	"sort"
	"sync"
	"time"
)

// DCElectionState 数据中心选举状态
type DCElectionState struct {
	// 主数据中心是否有活跃的Leader
	PrimaryDCHasLeader bool

	// 最后一次从主DC收到心跳的时间
	LastPrimaryDCHeartbeat time.Time

	// 当前数据中心的Leader候选人
	LocalCandidates []NodeID

	// 跨数据中心延迟统计
	CrossDCLatencies map[DataCenterID]time.Duration
}

// DCRaftExtension 数据中心感知的Raft扩展
type DCRaftExtension struct {
	mu sync.RWMutex

	// 基础配置
	config *Config
	nodeID NodeID
	logger *log.Logger

	// 数据中心状态
	localDataCenter DataCenterID
	electionState   *DCElectionState

	// 异步复制管理
	asyncReplicationManager *AsyncReplicationManager

	// 跨DC网络延迟监控
	latencyMonitor *CrossDCLatencyMonitor
}

// AsyncReplicationManager 异步复制管理器
type AsyncReplicationManager struct {
	mu sync.RWMutex

	// 待异步复制的日志条目缓冲区
	pendingEntries []LogEntry

	// 异步复制批次大小
	batchSize int

	// 异步复制延迟
	replicationDelay time.Duration

	// 压缩是否启用
	compressionEnabled bool

	// 异步复制定时器
	replicationTimer *time.Timer

	// 停止信号
	stopCh chan struct{}
}

// CrossDCLatencyMonitor 跨数据中心延迟监控
type CrossDCLatencyMonitor struct {
	mu sync.RWMutex

	// 各数据中心的延迟统计
	latencies map[DataCenterID]*LatencyStats

	// 延迟监控间隔
	monitorInterval time.Duration

	// 停止信号
	stopCh chan struct{}
}

// LatencyStats 延迟统计信息
type LatencyStats struct {
	Min     time.Duration
	Max     time.Duration
	Avg     time.Duration
	P95     time.Duration
	Samples []time.Duration
}

// NewDCRaftExtension 创建数据中心感知的Raft扩展
func NewDCRaftExtension(config *Config, nodeID NodeID) *DCRaftExtension {
	logger := log.New(log.Writer(), fmt.Sprintf("[dc-raft-%s] ", nodeID), log.LstdFlags)

	// 确定本地数据中心
	var localDC DataCenterID = "default"
	for _, server := range config.Servers {
		if server.ID == nodeID {
			localDC = server.DataCenter
			break
		}
	}

	extension := &DCRaftExtension{
		config:          config,
		nodeID:          nodeID,
		logger:          logger,
		localDataCenter: localDC,
		electionState: &DCElectionState{
			CrossDCLatencies: make(map[DataCenterID]time.Duration),
		},
	}

	// 初始化异步复制管理器
	if config.MultiDC != nil && config.MultiDC.Enabled {
		extension.asyncReplicationManager = NewAsyncReplicationManager(config.MultiDC)
		extension.latencyMonitor = NewCrossDCLatencyMonitor(config.MultiDC)
	}

	return extension
}

// NewAsyncReplicationManager 创建异步复制管理器
func NewAsyncReplicationManager(multiDCConfig *MultiDCConfig) *AsyncReplicationManager {
	localDC := multiDCConfig.LocalDataCenter
	if localDC == nil {
		return nil
	}

	// 确保复制延迟至少为1毫秒，避免NewTicker panic
	replicationDelay := localDC.AsyncReplicationDelay
	if replicationDelay <= 0 {
		replicationDelay = time.Millisecond * 100 // 默认100ms
	}

	return &AsyncReplicationManager{
		pendingEntries:     make([]LogEntry, 0),
		batchSize:          localDC.MaxAsyncBatchSize,
		replicationDelay:   replicationDelay,
		compressionEnabled: localDC.EnableCompression,
		stopCh:             make(chan struct{}),
	}
}

// NewCrossDCLatencyMonitor 创建跨数据中心延迟监控
func NewCrossDCLatencyMonitor(multiDCConfig *MultiDCConfig) *CrossDCLatencyMonitor {
	return &CrossDCLatencyMonitor{
		latencies:       make(map[DataCenterID]*LatencyStats),
		monitorInterval: time.Second * 5, // 默认5秒监控间隔
		stopCh:          make(chan struct{}),
	}
}

// ShouldStartElection 判断是否应该开始选举
// 实现数据中心优先级选举逻辑
func (ext *DCRaftExtension) ShouldStartElection() bool {
	ext.mu.RLock()
	defer ext.mu.RUnlock()

	// 如果未启用多数据中心模式，使用标准选举逻辑
	if ext.config.MultiDC == nil || !ext.config.MultiDC.Enabled {
		return true
	}

	// 如果未启用数据中心优先级选举，使用标准选举逻辑
	if !ext.config.MultiDC.DCPriorityElection {
		return true
	}

	// 获取本地数据中心配置
	localDCConfig := ext.config.MultiDC.LocalDataCenter
	if localDCConfig == nil {
		return true
	}

	// 如果本地是主数据中心，优先开始选举
	if localDCConfig.IsPrimary {
		return true
	}

	// 如果本地是辅助数据中心，检查主数据中心状态
	// 如果LastPrimaryDCHeartbeat是零值，说明还没有收到过主DC心跳，允许选举
	if ext.electionState.LastPrimaryDCHeartbeat.IsZero() {
		return true
	}

	timeSinceLastPrimaryHeartbeat := time.Since(ext.electionState.LastPrimaryDCHeartbeat)
	maxLatency := ext.config.MultiDC.MaxCrossDCLatency

	// 如果主数据中心长时间没有响应，允许辅助数据中心开始选举
	if timeSinceLastPrimaryHeartbeat > maxLatency*3 {
		ext.logger.Printf("主数据中心长时间无响应，辅助数据中心开始选举")
		return true
	}

	return false
}

// ProcessAppendEntries 处理AppendEntries请求，支持数据中心感知
func (ext *DCRaftExtension) ProcessAppendEntries(req *AppendEntriesRequest) (*AppendEntriesResponse, bool) {
	ext.mu.Lock()
	defer ext.mu.Unlock()

	// 记录跨数据中心心跳
	ext.recordCrossDCHeartbeat(req.LeaderID)

	// 如果未启用多数据中心模式，返回false表示使用标准处理
	if ext.config.MultiDC == nil || !ext.config.MultiDC.Enabled {
		return nil, false
	}

	// 检查是否为跨数据中心复制
	leaderDC := ext.getNodeDataCenter(req.LeaderID)
	if leaderDC == ext.localDataCenter {
		// 同数据中心内的复制，使用标准处理
		return nil, false
	}

	// 跨数据中心复制，可能需要特殊处理
	// 这里可以添加压缩解压、批量处理等逻辑

	// 暂时返回false，使用标准处理
	return nil, false
}

// AddToAsyncReplication 添加日志条目到异步复制队列
func (ext *DCRaftExtension) AddToAsyncReplication(entries []LogEntry) {
	if ext.asyncReplicationManager == nil {
		return
	}

	ext.asyncReplicationManager.mu.Lock()
	defer ext.asyncReplicationManager.mu.Unlock()

	ext.asyncReplicationManager.pendingEntries = append(
		ext.asyncReplicationManager.pendingEntries,
		entries...,
	)

	// 如果达到批次大小，立即触发复制
	if len(ext.asyncReplicationManager.pendingEntries) >= ext.asyncReplicationManager.batchSize {
		ext.triggerAsyncReplication()
	}
}

// triggerAsyncReplication 触发异步复制
func (ext *DCRaftExtension) triggerAsyncReplication() {
	// 重置定时器
	if ext.asyncReplicationManager.replicationTimer != nil {
		ext.asyncReplicationManager.replicationTimer.Stop()
	}

	// 启动新的定时器
	ext.asyncReplicationManager.replicationTimer = time.AfterFunc(
		ext.asyncReplicationManager.replicationDelay,
		ext.performAsyncReplication,
	)
}

// performAsyncReplication 执行异步复制
func (ext *DCRaftExtension) performAsyncReplication() {
	ext.asyncReplicationManager.mu.Lock()
	defer ext.asyncReplicationManager.mu.Unlock()

	if len(ext.asyncReplicationManager.pendingEntries) == 0 {
		return
	}

	// 复制待处理的条目
	entries := make([]LogEntry, len(ext.asyncReplicationManager.pendingEntries))
	copy(entries, ext.asyncReplicationManager.pendingEntries)

	// 清空待处理队列
	ext.asyncReplicationManager.pendingEntries = ext.asyncReplicationManager.pendingEntries[:0]

	// 执行异步复制（这里需要实际的网络发送逻辑）
	ext.logger.Printf("执行异步复制，条目数量: %d", len(entries))

	// TODO: 实现实际的跨数据中心网络发送逻辑
}

// recordCrossDCHeartbeat 记录跨数据中心心跳
func (ext *DCRaftExtension) recordCrossDCHeartbeat(leaderID NodeID) {
	leaderDC := ext.getNodeDataCenter(leaderID)
	if leaderDC == ext.localDataCenter {
		return // 同数据中心内的心跳
	}

	// 检查是否为主数据中心的心跳
	if ext.config.MultiDC != nil && ext.config.MultiDC.DataCenters != nil {
		if dcConfig, exists := ext.config.MultiDC.DataCenters[leaderDC]; exists && dcConfig.IsPrimary {
			ext.electionState.LastPrimaryDCHeartbeat = time.Now()
			ext.electionState.PrimaryDCHasLeader = true
		}
	}
}

// getNodeDataCenter 获取节点的数据中心
func (ext *DCRaftExtension) getNodeDataCenter(nodeID NodeID) DataCenterID {
	for _, server := range ext.config.Servers {
		if server.ID == nodeID {
			return server.DataCenter
		}
	}
	return "unknown"
}

// Start 启动数据中心扩展
func (ext *DCRaftExtension) Start() error {
	ext.logger.Printf("启动数据中心感知Raft扩展，本地DC: %s", ext.localDataCenter)

	// 启动异步复制管理器
	if ext.asyncReplicationManager != nil {
		go ext.asyncReplicationManager.start()
	}

	// 启动延迟监控
	if ext.latencyMonitor != nil {
		go ext.latencyMonitor.start()
	}

	return nil
}

// Stop 停止数据中心扩展
func (ext *DCRaftExtension) Stop() error {
	ext.logger.Printf("停止数据中心感知Raft扩展")

	// 停止异步复制管理器
	if ext.asyncReplicationManager != nil {
		close(ext.asyncReplicationManager.stopCh)
	}

	// 停止延迟监控
	if ext.latencyMonitor != nil {
		close(ext.latencyMonitor.stopCh)
	}

	return nil
}

// start 启动异步复制管理器
func (arm *AsyncReplicationManager) start() {
	ticker := time.NewTicker(arm.replicationDelay)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			arm.mu.Lock()
			if len(arm.pendingEntries) > 0 {
				// 触发异步复制
				// TODO: 实现实际的异步复制逻辑
			}
			arm.mu.Unlock()

		case <-arm.stopCh:
			return
		}
	}
}

// start 启动延迟监控
func (lm *CrossDCLatencyMonitor) start() {
	ticker := time.NewTicker(lm.monitorInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			lm.updateLatencyStats()

		case <-lm.stopCh:
			return
		}
	}
}

// updateLatencyStats 更新延迟统计
func (lm *CrossDCLatencyMonitor) updateLatencyStats() {
	lm.mu.Lock()
	defer lm.mu.Unlock()

	// TODO: 实现延迟统计更新逻辑
	// 这里可以ping各个数据中心的节点来测量延迟
}

// GetDCElectionState 获取数据中心选举状态
func (ext *DCRaftExtension) GetDCElectionState() *DCElectionState {
	ext.mu.RLock()
	defer ext.mu.RUnlock()

	// 返回状态的副本
	state := &DCElectionState{
		PrimaryDCHasLeader:     ext.electionState.PrimaryDCHasLeader,
		LastPrimaryDCHeartbeat: ext.electionState.LastPrimaryDCHeartbeat,
		LocalCandidates:        make([]NodeID, len(ext.electionState.LocalCandidates)),
		CrossDCLatencies:       make(map[DataCenterID]time.Duration),
	}

	copy(state.LocalCandidates, ext.electionState.LocalCandidates)
	for dc, latency := range ext.electionState.CrossDCLatencies {
		state.CrossDCLatencies[dc] = latency
	}

	return state
}

// GetLatencyStats 获取延迟统计信息
func (ext *DCRaftExtension) GetLatencyStats() map[DataCenterID]*LatencyStats {
	if ext.latencyMonitor == nil {
		return nil
	}

	ext.latencyMonitor.mu.RLock()
	defer ext.latencyMonitor.mu.RUnlock()

	stats := make(map[DataCenterID]*LatencyStats)
	for dc, latency := range ext.latencyMonitor.latencies {
		// 返回副本
		stats[dc] = &LatencyStats{
			Min:     latency.Min,
			Max:     latency.Max,
			Avg:     latency.Avg,
			P95:     latency.P95,
			Samples: make([]time.Duration, len(latency.Samples)),
		}
		copy(stats[dc].Samples, latency.Samples)
	}

	return stats
}

// calculateP95 计算P95延迟
func calculateP95(samples []time.Duration) time.Duration {
	if len(samples) == 0 {
		return 0
	}

	sorted := make([]time.Duration, len(samples))
	copy(sorted, samples)
	sort.Slice(sorted, func(i, j int) bool {
		return sorted[i] < sorted[j]
	})

	index := int(float64(len(sorted)) * 0.95)
	if index >= len(sorted) {
		index = len(sorted) - 1
	}

	return sorted[index]
}
