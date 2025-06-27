/*
* @Author: Lzww0608
* @Date: 2025-6-27 21:32:15
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-27 21:32:15
* @Description: ConcordKV 跨数据中心日志复制优化
 */

package raft

import (
	"bytes"
	"compress/gzip"
	"context"
	"fmt"
	"log"
	"sync"
	"time"
)

// CrossDCReplicationManager 跨数据中心复制管理器
type CrossDCReplicationManager struct {
	mu sync.RWMutex

	// 基础配置
	nodeID    NodeID
	config    *Config
	transport Transport
	logger    *log.Logger

	// 复制状态
	targetDCs        map[DataCenterID]*DCReplicationTarget
	replicationQueue chan *ReplicationBatch

	// 性能优化
	compressionEnabled bool
	batchSize          int
	batchTimeout       time.Duration
	maxRetries         int

	// 控制
	ctx    context.Context
	cancel context.CancelFunc
	wg     sync.WaitGroup
	stopCh chan struct{}

	// 统计信息
	stats *CrossDCReplicationStats
}

// DCReplicationTarget 数据中心复制目标
type DCReplicationTarget struct {
	mu sync.RWMutex

	// 目标信息
	DataCenter DataCenterID
	Nodes      []NodeID
	IsPrimary  bool

	// 复制状态
	LastReplicatedIndex LogIndex
	LastReplicatedTerm  Term
	ReplicationLag      time.Duration

	// 网络状态
	IsConnected   bool
	LastHeartbeat time.Time
	FailureCount  int64
	RetryBackoff  time.Duration

	// 批量缓冲
	PendingEntries []LogEntry
	LastBatchSent  time.Time
}

// ReplicationBatch 复制批次
type ReplicationBatch struct {
	TargetDC       DataCenterID
	Entries        []LogEntry
	CompressedData []byte
	Checksum       uint32
	CreatedAt      time.Time
	RetryCount     int
}

// CrossDCReplicationStats 跨DC复制统计
type CrossDCReplicationStats struct {
	mu sync.RWMutex

	// 总体统计
	TotalBatchesSent       int64
	TotalEntriesReplicated int64
	TotalBytesTransferred  int64

	// 性能统计
	AverageLatency   time.Duration
	CompressionRatio float64
	SuccessRate      float64

	// 错误统计
	NetworkErrors     int64
	CompressionErrors int64
	TimeoutErrors     int64

	// 按DC统计
	DCStats map[DataCenterID]*DCReplicationStat
}

// DCReplicationStat 单个DC复制统计
type DCReplicationStat struct {
	BatchesSent       int64
	EntriesReplicated int64
	BytesTransferred  int64
	AverageLatency    time.Duration
	ErrorCount        int64
	LastSuccessTime   time.Time
}

// CompressedAppendEntriesRequest 压缩的AppendEntries请求
type CompressedAppendEntriesRequest struct {
	// 标准字段
	Term         Term     `json:"term"`
	LeaderID     NodeID   `json:"leaderId"`
	PrevLogIndex LogIndex `json:"prevLogIndex"`
	PrevLogTerm  Term     `json:"prevLogTerm"`
	LeaderCommit LogIndex `json:"leaderCommit"`

	// 压缩优化字段
	IsCompressed    bool   `json:"isCompressed"`
	CompressedData  []byte `json:"compressedData,omitempty"`
	OriginalSize    int    `json:"originalSize"`
	CompressionType string `json:"compressionType"`
	Checksum        uint32 `json:"checksum"`

	// 批量优化字段
	BatchID     string `json:"batchId"`
	BatchSize   int    `json:"batchSize"`
	SequenceNum int    `json:"sequenceNum"`

	// DC感知字段
	SourceDC DataCenterID `json:"sourceDC"`
	TargetDC DataCenterID `json:"targetDC"`
	Priority int          `json:"priority"` // 1=高优先级(主DC), 2=普通优先级
}

// CompressedAppendEntriesResponse 压缩的AppendEntries响应
type CompressedAppendEntriesResponse struct {
	// 标准字段
	Term          Term     `json:"term"`
	Success       bool     `json:"success"`
	ConflictIndex LogIndex `json:"conflictIndex"`
	ConflictTerm  Term     `json:"conflictTerm"`

	// 优化反馈字段
	ProcessingTime    time.Duration `json:"processingTime"`
	DecompressionTime time.Duration `json:"decompressionTime"`
	NetworkLatency    time.Duration `json:"networkLatency"`

	// 批量确认字段
	BatchID            string   `json:"batchId"`
	ProcessedCount     int      `json:"processedCount"`
	LastProcessedIndex LogIndex `json:"lastProcessedIndex"`
}

// NewCrossDCReplicationManager 创建跨DC复制管理器
func NewCrossDCReplicationManager(nodeID NodeID, config *Config, transport Transport) *CrossDCReplicationManager {
	ctx, cancel := context.WithCancel(context.Background())

	manager := &CrossDCReplicationManager{
		nodeID:             nodeID,
		config:             config,
		transport:          transport,
		logger:             log.New(log.Writer(), fmt.Sprintf("[cross-dc-%s] ", nodeID), log.LstdFlags),
		targetDCs:          make(map[DataCenterID]*DCReplicationTarget),
		replicationQueue:   make(chan *ReplicationBatch, 1000),
		compressionEnabled: true,
		batchSize:          100,
		batchTimeout:       time.Millisecond * 50,
		maxRetries:         3,
		ctx:                ctx,
		cancel:             cancel,
		stopCh:             make(chan struct{}),
		stats: &CrossDCReplicationStats{
			DCStats: make(map[DataCenterID]*DCReplicationStat),
		},
	}

	// 初始化目标DC
	if config.MultiDC != nil && config.MultiDC.Enabled {
		manager.initializeTargetDCs()
	}

	return manager
}

// initializeTargetDCs 初始化目标数据中心
func (m *CrossDCReplicationManager) initializeTargetDCs() {
	localDC := m.config.MultiDC.LocalDataCenter.ID

	// 按数据中心分组节点
	dcNodes := make(map[DataCenterID][]NodeID)
	for _, server := range m.config.Servers {
		if server.DataCenter != localDC {
			dcNodes[server.DataCenter] = append(dcNodes[server.DataCenter], server.ID)
		}
	}

	// 创建复制目标
	for dcID, nodes := range dcNodes {
		isPrimary := false
		if dcConfig, exists := m.config.MultiDC.DataCenters[dcID]; exists {
			isPrimary = dcConfig.IsPrimary
		}

		target := &DCReplicationTarget{
			DataCenter:          dcID,
			Nodes:               nodes,
			IsPrimary:           isPrimary,
			LastReplicatedIndex: 0,
			LastReplicatedTerm:  0,
			IsConnected:         true,
			PendingEntries:      make([]LogEntry, 0),
			RetryBackoff:        time.Millisecond * 100,
		}

		m.targetDCs[dcID] = target

		// 初始化统计信息
		m.stats.DCStats[dcID] = &DCReplicationStat{}

		m.logger.Printf("初始化复制目标: DC=%s, 节点数=%d, 主DC=%v", dcID, len(nodes), isPrimary)
	}
}

// Start 启动跨DC复制管理器
func (m *CrossDCReplicationManager) Start() error {
	m.logger.Printf("启动跨DC复制管理器")

	// 启动复制工作线程
	m.wg.Add(3)
	go m.batchProcessingLoop()
	go m.replicationWorkerLoop()
	go m.healthMonitoringLoop()

	return nil
}

// Stop 停止跨DC复制管理器
func (m *CrossDCReplicationManager) Stop() error {
	m.logger.Printf("停止跨DC复制管理器")

	// 发送停止信号
	close(m.stopCh)
	m.cancel()

	// 等待工作线程结束
	m.wg.Wait()

	// 关闭队列
	close(m.replicationQueue)

	return nil
}

// ReplicateEntries 复制日志条目到其他数据中心
func (m *CrossDCReplicationManager) ReplicateEntries(entries []LogEntry) error {
	if len(entries) == 0 {
		return nil
	}

	m.mu.RLock()
	defer m.mu.RUnlock()

	// 为每个目标DC创建复制批次
	for dcID, target := range m.targetDCs {
		// 检查是否需要复制到此DC
		if !m.shouldReplicateToDC(target, entries) {
			continue
		}

		// 创建复制批次
		batch := &ReplicationBatch{
			TargetDC:   dcID,
			Entries:    make([]LogEntry, len(entries)),
			CreatedAt:  time.Now(),
			RetryCount: 0,
		}
		copy(batch.Entries, entries)

		// 如果启用压缩，压缩数据
		if m.compressionEnabled {
			if err := m.compressBatch(batch); err != nil {
				m.logger.Printf("压缩批次失败: %v", err)
				m.stats.mu.Lock()
				m.stats.CompressionErrors++
				m.stats.mu.Unlock()
				continue
			}
		}

		// 发送到复制队列
		select {
		case m.replicationQueue <- batch:
			m.logger.Printf("添加复制批次到队列: DC=%s, 条目数=%d", dcID, len(entries))
		case <-m.ctx.Done():
			return fmt.Errorf("复制管理器已停止")
		default:
			m.logger.Printf("复制队列已满，跳过批次: DC=%s", dcID)
		}
	}

	return nil
}

// shouldReplicateToDC 判断是否应该复制到指定DC
func (m *CrossDCReplicationManager) shouldReplicateToDC(target *DCReplicationTarget, entries []LogEntry) bool {
	target.mu.RLock()
	defer target.mu.RUnlock()

	// 检查连接状态
	if !target.IsConnected {
		return false
	}

	// 检查是否有新的条目需要复制
	if len(entries) == 0 {
		return false
	}

	// 检查最后一个条目的索引
	lastEntry := entries[len(entries)-1]
	if lastEntry.Index <= target.LastReplicatedIndex {
		return false // 已经复制过了
	}

	return true
}

// compressBatch 压缩复制批次
func (m *CrossDCReplicationManager) compressBatch(batch *ReplicationBatch) error {
	// 序列化日志条目
	data, err := m.serializeEntries(batch.Entries)
	if err != nil {
		return fmt.Errorf("序列化条目失败: %w", err)
	}

	// 使用gzip压缩
	var compressed bytes.Buffer
	gzipWriter := gzip.NewWriter(&compressed)

	if _, err := gzipWriter.Write(data); err != nil {
		gzipWriter.Close()
		return fmt.Errorf("压缩数据失败: %w", err)
	}

	if err := gzipWriter.Close(); err != nil {
		return fmt.Errorf("关闭压缩器失败: %w", err)
	}

	batch.CompressedData = compressed.Bytes()
	batch.Checksum = m.calculateChecksum(data)

	// 更新压缩比统计
	compressionRatio := float64(len(batch.CompressedData)) / float64(len(data))
	m.stats.mu.Lock()
	m.stats.CompressionRatio = (m.stats.CompressionRatio + compressionRatio) / 2.0
	m.stats.mu.Unlock()

	m.logger.Printf("批次压缩完成: 原始大小=%d, 压缩后=%d, 压缩比=%.2f",
		len(data), len(batch.CompressedData), compressionRatio)

	return nil
}

// serializeEntries 序列化日志条目
func (m *CrossDCReplicationManager) serializeEntries(entries []LogEntry) ([]byte, error) {
	// 简单的序列化实现，实际项目中可以使用protobuf或其他高效格式
	var buffer bytes.Buffer

	for _, entry := range entries {
		// 写入条目数据（这里简化处理）
		entryData := fmt.Sprintf("%d:%d:%d:%s\n",
			entry.Index, entry.Term, entry.Type, string(entry.Data))
		buffer.WriteString(entryData)
	}

	return buffer.Bytes(), nil
}

// calculateChecksum 计算校验和
func (m *CrossDCReplicationManager) calculateChecksum(data []byte) uint32 {
	// 简单的校验和实现
	var checksum uint32
	for _, b := range data {
		checksum += uint32(b)
	}
	return checksum
}

// batchProcessingLoop 批量处理循环
func (m *CrossDCReplicationManager) batchProcessingLoop() {
	defer m.wg.Done()

	ticker := time.NewTicker(m.batchTimeout)
	defer ticker.Stop()

	for {
		select {
		case <-m.stopCh:
			return
		case <-ticker.C:
			// 定期检查是否有待处理的批次
			m.processPendingBatches()
		}
	}
}

// processPendingBatches 处理待处理的批次
func (m *CrossDCReplicationManager) processPendingBatches() {
	m.mu.Lock()
	defer m.mu.Unlock()

	for dcID, target := range m.targetDCs {
		target.mu.Lock()
		if len(target.PendingEntries) > 0 {
			// 创建批次
			batch := &ReplicationBatch{
				TargetDC:   dcID,
				Entries:    make([]LogEntry, len(target.PendingEntries)),
				CreatedAt:  time.Now(),
				RetryCount: 0,
			}
			copy(batch.Entries, target.PendingEntries)

			// 清空待处理条目
			target.PendingEntries = target.PendingEntries[:0]
			target.LastBatchSent = time.Now()

			target.mu.Unlock()

			// 压缩并发送
			if m.compressionEnabled {
				if err := m.compressBatch(batch); err != nil {
					m.logger.Printf("压缩待处理批次失败: %v", err)
					continue
				}
			}

			// 发送到队列
			select {
			case m.replicationQueue <- batch:
				m.logger.Printf("发送待处理批次: DC=%s, 条目数=%d", dcID, len(batch.Entries))
			default:
				m.logger.Printf("队列已满，丢弃待处理批次: DC=%s", dcID)
			}
		} else {
			target.mu.Unlock()
		}
	}
}

// replicationWorkerLoop 复制工作循环
func (m *CrossDCReplicationManager) replicationWorkerLoop() {
	defer m.wg.Done()

	for {
		select {
		case <-m.stopCh:
			return
		case batch := <-m.replicationQueue:
			if batch == nil {
				return // 队列已关闭
			}
			m.processBatch(batch)
		}
	}
}

// processBatch 处理复制批次
func (m *CrossDCReplicationManager) processBatch(batch *ReplicationBatch) {
	startTime := time.Now()

	// 获取目标DC的节点列表
	target, exists := m.targetDCs[batch.TargetDC]
	if !exists {
		m.logger.Printf("目标DC不存在: %s", batch.TargetDC)
		return
	}

	target.mu.RLock()
	nodes := make([]NodeID, len(target.Nodes))
	copy(nodes, target.Nodes)
	target.mu.RUnlock()

	// 尝试发送到目标DC的节点
	success := false
	for _, nodeID := range nodes {
		if err := m.sendBatchToNode(batch, nodeID); err != nil {
			m.logger.Printf("发送批次到节点失败: DC=%s, 节点=%s, 错误=%v",
				batch.TargetDC, nodeID, err)
			continue
		}
		success = true
		break // 成功发送到一个节点即可
	}

	// 更新统计信息
	m.updateReplicationStats(batch, success, time.Since(startTime))

	// 如果失败且重试次数未达上限，重新入队
	if !success && batch.RetryCount < m.maxRetries {
		batch.RetryCount++

		// 指数退避
		retryDelay := time.Duration(1<<batch.RetryCount) * time.Millisecond * 100
		time.AfterFunc(retryDelay, func() {
			select {
			case m.replicationQueue <- batch:
				m.logger.Printf("重试复制批次: DC=%s, 重试次数=%d",
					batch.TargetDC, batch.RetryCount)
			case <-m.ctx.Done():
				// 管理器已停止
			}
		})
	}
}

// sendBatchToNode 发送批次到指定节点
func (m *CrossDCReplicationManager) sendBatchToNode(batch *ReplicationBatch, nodeID NodeID) error {
	// 构造压缩的AppendEntries请求
	req := &CompressedAppendEntriesRequest{
		Term:            1, // 实际应该从Raft状态获取
		LeaderID:        m.nodeID,
		PrevLogIndex:    0, // 实际应该计算
		PrevLogTerm:     0, // 实际应该计算
		LeaderCommit:    0, // 实际应该从Raft状态获取
		IsCompressed:    m.compressionEnabled,
		CompressedData:  batch.CompressedData,
		OriginalSize:    len(batch.Entries),
		CompressionType: "gzip",
		Checksum:        batch.Checksum,
		BatchID:         fmt.Sprintf("%s-%d", batch.TargetDC, time.Now().UnixNano()),
		BatchSize:       len(batch.Entries),
		SequenceNum:     1,
		SourceDC:        m.config.MultiDC.LocalDataCenter.ID,
		TargetDC:        batch.TargetDC,
		Priority:        m.getReplicationPriority(batch.TargetDC),
	}

	// 发送请求（这里简化处理，实际需要实现网络发送）
	ctx, cancel := context.WithTimeout(m.ctx, time.Second*5)
	defer cancel()

	// 模拟网络发送
	_ = ctx
	_ = req

	m.logger.Printf("发送压缩批次到节点: 节点=%s, 批次ID=%s, 压缩=%v, 大小=%d",
		nodeID, req.BatchID, req.IsCompressed, len(req.CompressedData))

	return nil // 模拟成功
}

// getReplicationPriority 获取复制优先级
func (m *CrossDCReplicationManager) getReplicationPriority(targetDC DataCenterID) int {
	if dcConfig, exists := m.config.MultiDC.DataCenters[targetDC]; exists && dcConfig.IsPrimary {
		return 1 // 高优先级
	}
	return 2 // 普通优先级
}

// updateReplicationStats 更新复制统计信息
func (m *CrossDCReplicationManager) updateReplicationStats(batch *ReplicationBatch, success bool, duration time.Duration) {
	m.stats.mu.Lock()
	defer m.stats.mu.Unlock()

	// 更新总体统计
	m.stats.TotalBatchesSent++
	if success {
		m.stats.TotalEntriesReplicated += int64(len(batch.Entries))
		m.stats.TotalBytesTransferred += int64(len(batch.CompressedData))

		// 更新平均延迟
		if m.stats.AverageLatency == 0 {
			m.stats.AverageLatency = duration
		} else {
			m.stats.AverageLatency = (m.stats.AverageLatency + duration) / 2
		}
	}

	// 更新成功率
	successCount := m.stats.TotalBatchesSent
	if !success {
		successCount--
	}
	m.stats.SuccessRate = float64(successCount) / float64(m.stats.TotalBatchesSent)

	// 更新DC级别统计
	dcStat := m.stats.DCStats[batch.TargetDC]
	dcStat.BatchesSent++
	if success {
		dcStat.EntriesReplicated += int64(len(batch.Entries))
		dcStat.BytesTransferred += int64(len(batch.CompressedData))
		dcStat.LastSuccessTime = time.Now()

		// 更新DC平均延迟
		if dcStat.AverageLatency == 0 {
			dcStat.AverageLatency = duration
		} else {
			dcStat.AverageLatency = (dcStat.AverageLatency + duration) / 2
		}
	} else {
		dcStat.ErrorCount++
	}
}

// healthMonitoringLoop 健康监控循环
func (m *CrossDCReplicationManager) healthMonitoringLoop() {
	defer m.wg.Done()

	ticker := time.NewTicker(time.Second * 10) // 每10秒检查一次
	defer ticker.Stop()

	for {
		select {
		case <-m.stopCh:
			return
		case <-ticker.C:
			m.checkDCHealth()
		}
	}
}

// checkDCHealth 检查数据中心健康状态
func (m *CrossDCReplicationManager) checkDCHealth() {
	m.mu.Lock()
	defer m.mu.Unlock()

	for dcID, target := range m.targetDCs {
		target.mu.Lock()

		// 检查最后心跳时间
		timeSinceLastHeartbeat := time.Since(target.LastHeartbeat)
		if timeSinceLastHeartbeat > time.Minute*2 { // 2分钟无心跳认为不健康
			if target.IsConnected {
				m.logger.Printf("DC %s 连接状态变为不健康", dcID)
				target.IsConnected = false
				target.FailureCount++
			}
		} else {
			if !target.IsConnected {
				m.logger.Printf("DC %s 连接状态恢复健康", dcID)
				target.IsConnected = true
				target.FailureCount = 0
				target.RetryBackoff = time.Millisecond * 100 // 重置退避时间
			}
		}

		target.mu.Unlock()
	}
}

// GetReplicationStats 获取复制统计信息
func (m *CrossDCReplicationManager) GetReplicationStats() *CrossDCReplicationStats {
	m.stats.mu.RLock()
	defer m.stats.mu.RUnlock()

	// 创建副本以避免并发访问问题
	stats := &CrossDCReplicationStats{
		TotalBatchesSent:       m.stats.TotalBatchesSent,
		TotalEntriesReplicated: m.stats.TotalEntriesReplicated,
		TotalBytesTransferred:  m.stats.TotalBytesTransferred,
		AverageLatency:         m.stats.AverageLatency,
		CompressionRatio:       m.stats.CompressionRatio,
		SuccessRate:            m.stats.SuccessRate,
		NetworkErrors:          m.stats.NetworkErrors,
		CompressionErrors:      m.stats.CompressionErrors,
		TimeoutErrors:          m.stats.TimeoutErrors,
		DCStats:                make(map[DataCenterID]*DCReplicationStat),
	}

	// 复制DC统计
	for dcID, dcStat := range m.stats.DCStats {
		stats.DCStats[dcID] = &DCReplicationStat{
			BatchesSent:       dcStat.BatchesSent,
			EntriesReplicated: dcStat.EntriesReplicated,
			BytesTransferred:  dcStat.BytesTransferred,
			AverageLatency:    dcStat.AverageLatency,
			ErrorCount:        dcStat.ErrorCount,
			LastSuccessTime:   dcStat.LastSuccessTime,
		}
	}

	return stats
}

// GetDCReplicationStatus 获取DC复制状态
func (m *CrossDCReplicationManager) GetDCReplicationStatus() map[DataCenterID]*DCReplicationTarget {
	m.mu.RLock()
	defer m.mu.RUnlock()

	status := make(map[DataCenterID]*DCReplicationTarget)
	for dcID, target := range m.targetDCs {
		target.mu.RLock()
		status[dcID] = &DCReplicationTarget{
			DataCenter:          target.DataCenter,
			Nodes:               append([]NodeID{}, target.Nodes...),
			IsPrimary:           target.IsPrimary,
			LastReplicatedIndex: target.LastReplicatedIndex,
			LastReplicatedTerm:  target.LastReplicatedTerm,
			ReplicationLag:      target.ReplicationLag,
			IsConnected:         target.IsConnected,
			LastHeartbeat:       target.LastHeartbeat,
			FailureCount:        target.FailureCount,
			RetryBackoff:        target.RetryBackoff,
		}
		target.mu.RUnlock()
	}

	return status
}

// UpdateDCHeartbeat 更新DC心跳
func (m *CrossDCReplicationManager) UpdateDCHeartbeat(dcID DataCenterID) {
	m.mu.RLock()
	target, exists := m.targetDCs[dcID]
	m.mu.RUnlock()

	if !exists {
		return
	}

	target.mu.Lock()
	target.LastHeartbeat = time.Now()
	if !target.IsConnected {
		m.logger.Printf("DC %s 心跳恢复", dcID)
		target.IsConnected = true
		target.FailureCount = 0
	}
	target.mu.Unlock()
}
