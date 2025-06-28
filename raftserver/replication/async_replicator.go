/*
* @Author: Lzww0608
* @Date: 2025-6-28 11:51:08
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-28 11:51:11
* @Description: ConcordKV 异步复制管理器 - 基于现有CrossDCReplicationManager扩展
 */

package replication

import (
	"context"
	"fmt"
	"log"
	"sync"
	"time"

	"raftserver/raft"
)

// AsyncReplicationConfig 异步复制配置
type AsyncReplicationConfig struct {
	// 基础配置
	Enabled bool `json:"enabled"`

	// 批量传输配置
	BatchSize            int  `json:"batchSize"`
	BatchTimeoutMs       int  `json:"batchTimeoutMs"`
	MaxBatchMemoryMB     int  `json:"maxBatchMemoryMB"`
	CompressionEnabled   bool `json:"compressionEnabled"`
	CompressionThreshold int  `json:"compressionThreshold"`

	// 延迟和重试配置
	MaxReplicationDelayMs int `json:"maxReplicationDelayMs"`
	RetryAttempts         int `json:"retryAttempts"`
	RetryBackoffMs        int `json:"retryBackoffMs"`
	HealthCheckIntervalMs int `json:"healthCheckIntervalMs"`

	// 监控和告警配置
	LatencyThresholdMs int     `json:"latencyThresholdMs"`
	ErrorRateThreshold float64 `json:"errorRateThreshold"`
	EnableMetrics      bool    `json:"enableMetrics"`
	EnableAlerts       bool    `json:"enableAlerts"`

	// 数据中心优先级配置
	DataCenterPriorities map[raft.DataCenterID]int `json:"dataCenterPriorities"`
}

// DefaultAsyncReplicationConfig 默认异步复制配置
func DefaultAsyncReplicationConfig() *AsyncReplicationConfig {
	return &AsyncReplicationConfig{
		Enabled:               true,
		BatchSize:             100,
		BatchTimeoutMs:        50,
		MaxBatchMemoryMB:      64,
		CompressionEnabled:    true,
		CompressionThreshold:  1024,
		MaxReplicationDelayMs: 5000,
		RetryAttempts:         3,
		RetryBackoffMs:        100,
		HealthCheckIntervalMs: 1000,
		LatencyThresholdMs:    200,
		ErrorRateThreshold:    0.05,
		EnableMetrics:         true,
		EnableAlerts:          true,
		DataCenterPriorities:  make(map[raft.DataCenterID]int),
	}
}

// 枚举定义
type ConnectionState int
type BatchStatus int

const (
	// 连接状态
	ConnectionHealthy ConnectionState = iota
	ConnectionDegraded
	ConnectionFailed
	ConnectionRecovering

	// 批次状态
	BatchPending BatchStatus = iota
	BatchInProgress
	BatchCompleted
	BatchFailed
	BatchRetrying
)

// AsyncReplicationTarget 异步复制目标
type AsyncReplicationTarget struct {
	mu sync.RWMutex

	// 目标信息
	DataCenter raft.DataCenterID
	Nodes      []raft.NodeID
	IsPrimary  bool
	Priority   int

	// 复制状态跟踪
	LastReplicatedIndex raft.LogIndex
	LastReplicatedTerm  raft.Term
	ReplicationLag      time.Duration
	IsHealthy           bool
	LastHealthCheck     time.Time

	// 网络状态
	ConnectionState ConnectionState
	FailureCount    int64
	LastSuccessTime time.Time
	RetryBackoff    time.Duration

	// 批量缓冲
	PendingEntries   []raft.LogEntry
	LastBatchSent    time.Time
	TotalBytesQueued int64
}

// AsyncReplicationBatch 异步复制批次
type AsyncReplicationBatch struct {
	// 批次标识
	BatchID   string
	TargetDC  raft.DataCenterID
	CreatedAt time.Time
	Priority  int

	// 日志条目
	Entries    []raft.LogEntry
	StartIndex raft.LogIndex
	EndIndex   raft.LogIndex

	// 优化字段
	CompressedData   []byte
	OriginalSize     int
	CompressionRatio float64
	Checksum         uint32

	// 状态跟踪
	AttemptCount int
	LastAttempt  time.Time
	Status       BatchStatus
}

// AsyncReplicationMetrics 异步复制指标
type AsyncReplicationMetrics struct {
	mu sync.RWMutex

	// 总体指标
	TotalBatchesSent       int64
	TotalEntriesReplicated int64
	TotalBytesTransferred  int64
	ReplicationThroughput  float64

	// 延迟指标
	AverageLatency time.Duration
	P95Latency     time.Duration
	P99Latency     time.Duration
	MaxLatency     time.Duration

	// 成功率指标
	SuccessRate float64
	ErrorRate   float64
	TimeoutRate float64

	// 性能指标
	CompressionRatio   float64
	NetworkUtilization float64
	CPUUsage           float64
	MemoryUsage        int64

	// 按DC分组指标
	DCMetrics map[raft.DataCenterID]*DCAsyncMetrics
}

type DCAsyncMetrics struct {
	EntriesReplicated int64
	AverageLatency    time.Duration
	ErrorCount        int64
	LastUpdateTime    time.Time
}

// AsyncReplicator 异步复制管理器
type AsyncReplicator struct {
	mu sync.RWMutex

	// 基础配置
	nodeID     raft.NodeID
	config     *AsyncReplicationConfig
	raftConfig *raft.Config
	transport  raft.Transport
	storage    raft.Storage
	logger     *log.Logger

	// 复制状态管理
	replicationTargets map[raft.DataCenterID]*AsyncReplicationTarget
	pendingBatches     chan *AsyncReplicationBatch

	// 监控和统计
	metrics *AsyncReplicationMetrics

	// 控制流
	ctx     context.Context
	cancel  context.CancelFunc
	wg      sync.WaitGroup
	running bool
	stopCh  chan struct{}
}

// NewAsyncReplicator 创建异步复制管理器
func NewAsyncReplicator(nodeID raft.NodeID, raftConfig *raft.Config, transport raft.Transport, storage raft.Storage) *AsyncReplicator {
	config := DefaultAsyncReplicationConfig()
	ctx, cancel := context.WithCancel(context.Background())

	replicator := &AsyncReplicator{
		nodeID:             nodeID,
		config:             config,
		raftConfig:         raftConfig,
		transport:          transport,
		storage:            storage,
		logger:             log.New(log.Writer(), fmt.Sprintf("[async-replicator-%s] ", nodeID), log.LstdFlags),
		replicationTargets: make(map[raft.DataCenterID]*AsyncReplicationTarget),
		pendingBatches:     make(chan *AsyncReplicationBatch, 1000),
		ctx:                ctx,
		cancel:             cancel,
		stopCh:             make(chan struct{}),
	}

	// 初始化组件
	replicator.initializeComponents()
	replicator.initializeTargets()

	return replicator
}

// initializeComponents 初始化组件
func (ar *AsyncReplicator) initializeComponents() {
	// 初始化指标收集器
	ar.metrics = &AsyncReplicationMetrics{
		DCMetrics: make(map[raft.DataCenterID]*DCAsyncMetrics),
	}
}

// initializeTargets 初始化复制目标
func (ar *AsyncReplicator) initializeTargets() {
	if ar.raftConfig.MultiDC == nil || !ar.raftConfig.MultiDC.Enabled {
		ar.logger.Printf("多DC未启用，跳过异步复制目标初始化")
		return
	}

	localDC := ar.raftConfig.MultiDC.LocalDataCenter.ID

	// 按数据中心分组节点
	dcNodes := make(map[raft.DataCenterID][]raft.NodeID)
	for _, server := range ar.raftConfig.Servers {
		if server.DataCenter != localDC {
			dcNodes[server.DataCenter] = append(dcNodes[server.DataCenter], server.ID)
		}
	}

	// 创建异步复制目标
	for dcID, nodes := range dcNodes {
		priority := ar.config.DataCenterPriorities[dcID]
		if priority == 0 {
			priority = 3 // 默认优先级
		}

		target := &AsyncReplicationTarget{
			DataCenter:          dcID,
			Nodes:               nodes,
			Priority:            priority,
			LastReplicatedIndex: 0,
			LastReplicatedTerm:  0,
			IsHealthy:           true,
			ConnectionState:     ConnectionHealthy,
			PendingEntries:      make([]raft.LogEntry, 0),
			RetryBackoff:        time.Duration(ar.config.RetryBackoffMs) * time.Millisecond,
		}

		ar.replicationTargets[dcID] = target

		// 初始化DC指标
		ar.metrics.DCMetrics[dcID] = &DCAsyncMetrics{
			LastUpdateTime: time.Now(),
		}

		ar.logger.Printf("初始化异步复制目标: DC=%s, 节点数=%d, 优先级=%d", dcID, len(nodes), priority)
	}
}

// Start 启动异步复制管理器
func (ar *AsyncReplicator) Start() error {
	ar.mu.Lock()
	defer ar.mu.Unlock()

	if ar.running {
		return fmt.Errorf("异步复制管理器已在运行")
	}

	ar.logger.Printf("启动异步复制管理器")

	// 启动工作线程
	ar.wg.Add(2)
	go ar.replicationWorkerLoop()
	go ar.metricsCollectionLoop()

	ar.running = true
	ar.logger.Printf("异步复制管理器启动成功")

	return nil
}

// Stop 停止异步复制管理器
func (ar *AsyncReplicator) Stop() error {
	ar.mu.Lock()
	defer ar.mu.Unlock()

	if !ar.running {
		return nil
	}

	ar.logger.Printf("停止异步复制管理器")

	// 发送停止信号
	close(ar.stopCh)
	ar.cancel()

	// 等待工作线程结束
	ar.wg.Wait()

	// 关闭队列
	close(ar.pendingBatches)

	ar.running = false
	ar.logger.Printf("异步复制管理器已停止")

	return nil
}

// ReplicateAsync 异步复制日志条目
func (ar *AsyncReplicator) ReplicateAsync(entries []raft.LogEntry) error {
	if len(entries) == 0 {
		return nil
	}

	ar.mu.RLock()
	defer ar.mu.RUnlock()

	// 为每个目标DC创建异步复制批次
	for dcID, target := range ar.replicationTargets {
		if !ar.shouldReplicateToDC(target, entries) {
			continue
		}

		batch := ar.createReplicationBatch(dcID, entries, target.Priority)

		select {
		case ar.pendingBatches <- batch:
			ar.logger.Printf("已加入异步复制队列: DC=%s, 条目数=%d", dcID, len(entries))
		case <-ar.ctx.Done():
			return fmt.Errorf("异步复制管理器已停止")
		default:
			ar.logger.Printf("警告: 异步复制队列已满, DC=%s", dcID)
		}
	}

	return nil
}

// GetReplicationStatus 获取复制状态
func (ar *AsyncReplicator) GetReplicationStatus() map[raft.DataCenterID]*AsyncReplicationTarget {
	ar.mu.RLock()
	defer ar.mu.RUnlock()

	status := make(map[raft.DataCenterID]*AsyncReplicationTarget)
	for dcID, target := range ar.replicationTargets {
		// 创建副本避免并发访问问题
		targetCopy := &AsyncReplicationTarget{}
		*targetCopy = *target
		status[dcID] = targetCopy
	}

	return status
}

// GetMetrics 获取复制指标
func (ar *AsyncReplicator) GetMetrics() *AsyncReplicationMetrics {
	ar.metrics.mu.RLock()
	defer ar.metrics.mu.RUnlock()

	// 创建指标副本
	metricsCopy := &AsyncReplicationMetrics{}
	*metricsCopy = *ar.metrics
	metricsCopy.DCMetrics = make(map[raft.DataCenterID]*DCAsyncMetrics)

	for dcID, dcMetrics := range ar.metrics.DCMetrics {
		dcMetricsCopy := &DCAsyncMetrics{}
		*dcMetricsCopy = *dcMetrics
		metricsCopy.DCMetrics[dcID] = dcMetricsCopy
	}

	return metricsCopy
}

// 内部方法实现
func (ar *AsyncReplicator) shouldReplicateToDC(target *AsyncReplicationTarget, entries []raft.LogEntry) bool {
	if !target.IsHealthy {
		return false
	}

	if len(entries) == 0 {
		return false
	}

	lastEntry := entries[len(entries)-1]
	return lastEntry.Index > target.LastReplicatedIndex
}

func (ar *AsyncReplicator) createReplicationBatch(dcID raft.DataCenterID, entries []raft.LogEntry, priority int) *AsyncReplicationBatch {
	batch := &AsyncReplicationBatch{
		BatchID:      fmt.Sprintf("batch-%d-%s", time.Now().UnixNano(), dcID),
		TargetDC:     dcID,
		CreatedAt:    time.Now(),
		Priority:     priority,
		Entries:      entries,
		StartIndex:   entries[0].Index,
		EndIndex:     entries[len(entries)-1].Index,
		Status:       BatchPending,
		AttemptCount: 0,
	}

	// 计算原始大小
	for _, entry := range entries {
		batch.OriginalSize += len(entry.Data)
	}

	return batch
}

// 工作线程循环
func (ar *AsyncReplicator) replicationWorkerLoop() {
	defer ar.wg.Done()
	ar.logger.Printf("复制工作循环已启动")

	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case batch := <-ar.pendingBatches:
			if batch != nil {
				ar.processBatch(batch)
			}
		case <-ticker.C:
			ar.performHealthChecks()
		case <-ar.stopCh:
			ar.logger.Printf("复制工作循环已停止")
			return
		}
	}
}

func (ar *AsyncReplicator) metricsCollectionLoop() {
	defer ar.wg.Done()
	ar.logger.Printf("指标收集循环已启动")

	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			ar.updateMetrics()
		case <-ar.stopCh:
			ar.logger.Printf("指标收集循环已停止")
			return
		}
	}
}

func (ar *AsyncReplicator) processBatch(batch *AsyncReplicationBatch) {
	start := time.Now()
	ar.logger.Printf("处理复制批次: %s, DC=%s", batch.BatchID, batch.TargetDC)

	target, exists := ar.replicationTargets[batch.TargetDC]
	if !exists {
		ar.logger.Printf("目标DC不存在: %s", batch.TargetDC)
		return
	}

	// 模拟异步复制处理
	batch.Status = BatchInProgress
	batch.AttemptCount++
	batch.LastAttempt = time.Now()

	// 更新目标状态
	target.mu.Lock()
	if len(batch.Entries) > 0 {
		target.LastReplicatedIndex = batch.EndIndex
		target.LastReplicatedTerm = batch.Entries[len(batch.Entries)-1].Term
	}
	target.LastSuccessTime = time.Now()
	target.IsHealthy = true
	target.mu.Unlock()

	// 更新指标
	ar.updateBatchMetrics(batch, time.Since(start))

	batch.Status = BatchCompleted
	ar.logger.Printf("批次处理完成: %s, 延迟=%v", batch.BatchID, time.Since(start))
}

func (ar *AsyncReplicator) performHealthChecks() {
	for dcID, target := range ar.replicationTargets {
		target.mu.Lock()
		target.LastHealthCheck = time.Now()

		// 简单的健康检查逻辑
		if time.Since(target.LastSuccessTime) > time.Duration(ar.config.MaxReplicationDelayMs)*time.Millisecond {
			target.IsHealthy = false
			target.ConnectionState = ConnectionDegraded
		} else {
			target.IsHealthy = true
			target.ConnectionState = ConnectionHealthy
		}
		target.mu.Unlock()

		ar.logger.Printf("健康检查: DC=%s, 健康状态=%t", dcID, target.IsHealthy)
	}
}

func (ar *AsyncReplicator) updateMetrics() {
	ar.metrics.mu.Lock()
	defer ar.metrics.mu.Unlock()

	// 更新总体指标
	ar.metrics.ReplicationThroughput = float64(ar.metrics.TotalEntriesReplicated) / time.Since(time.Now().Add(-5*time.Second)).Seconds()

	// 更新DC指标
	for dcID, dcMetrics := range ar.metrics.DCMetrics {
		dcMetrics.LastUpdateTime = time.Now()
		ar.logger.Printf("DC指标更新: %s, 复制条目数=%d", dcID, dcMetrics.EntriesReplicated)
	}
}

func (ar *AsyncReplicator) updateBatchMetrics(batch *AsyncReplicationBatch, latency time.Duration) {
	ar.metrics.mu.Lock()
	defer ar.metrics.mu.Unlock()

	ar.metrics.TotalBatchesSent++
	ar.metrics.TotalEntriesReplicated += int64(len(batch.Entries))
	ar.metrics.TotalBytesTransferred += int64(batch.OriginalSize)

	// 更新延迟指标
	if latency > ar.metrics.MaxLatency {
		ar.metrics.MaxLatency = latency
	}

	// 更新DC指标
	if dcMetrics, exists := ar.metrics.DCMetrics[batch.TargetDC]; exists {
		dcMetrics.EntriesReplicated += int64(len(batch.Entries))
		dcMetrics.AverageLatency = (dcMetrics.AverageLatency + latency) / 2
	}
}
