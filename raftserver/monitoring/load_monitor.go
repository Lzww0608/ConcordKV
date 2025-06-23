/*
* @Author: Lzww0608
* @Date: 2025-6-23 17:10:29
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-23 17:10:29
* @Description: ConcordKV 负载监控代理 - 扩展现有Raft指标系统支持动态负载均衡
 */

package monitoring

import (
	"context"
	"encoding/json"
	"log"
	"math"
	"runtime"
	"sort"
	"sync"
	"sync/atomic"
	"time"

	"raftserver/raft"
)

// LoadMonitorConfig 负载监控配置
type LoadMonitorConfig struct {
	// CollectionInterval 指标收集间隔
	CollectionInterval time.Duration

	// MetricsRetention 指标保留时间
	MetricsRetention time.Duration

	// HotKeyThreshold 热点键检测阈值(QPS)
	HotKeyThreshold float64

	// HotKeyWindowSize 热点键统计窗口大小
	HotKeyWindowSize time.Duration

	// LoadScoreWeights 负载评分权重配置
	LoadScoreWeights LoadScoreWeights

	// EnableHTTPAPI 是否启用HTTP API
	EnableHTTPAPI bool

	// HTTPPort HTTP API端口
	HTTPPort int
}

// LoadScoreWeights 负载评分权重配置
type LoadScoreWeights struct {
	QPS     float64 // QPS权重
	Storage float64 // 存储权重
	Memory  float64 // 内存权重
	CPU     float64 // CPU权重
	Network float64 // 网络权重
}

// DefaultLoadMonitorConfig 默认负载监控配置
func DefaultLoadMonitorConfig() *LoadMonitorConfig {
	return &LoadMonitorConfig{
		CollectionInterval: 5 * time.Second,
		MetricsRetention:   1 * time.Hour,
		HotKeyThreshold:    100.0, // 100 QPS
		HotKeyWindowSize:   1 * time.Minute,
		LoadScoreWeights: LoadScoreWeights{
			QPS:     0.3,
			Storage: 0.2,
			Memory:  0.2,
			CPU:     0.2,
			Network: 0.1,
		},
		EnableHTTPAPI: true,
		HTTPPort:      8080,
	}
}

// KeyAccessRecord 键访问记录
type KeyAccessRecord struct {
	Key        string    // 键名
	Count      int64     // 访问次数
	LastAccess time.Time // 最后访问时间
}

// LoadSnapshot 负载快照
type LoadSnapshot struct {
	NodeID    raft.NodeID       // 节点ID
	Timestamp time.Time         // 时间戳
	Metrics   *raft.LoadMetrics // 负载指标
}

// NodeInterface 节点接口 - 用于依赖注入和测试
type NodeInterface interface {
	GetMetrics() *raft.Metrics
	GetID() raft.NodeID
	AddEventListener(listener raft.EventListener)
}

// LoadMonitor 负载监控代理 - 复用现有Raft指标系统
type LoadMonitor struct {
	mu     sync.RWMutex
	config *LoadMonitorConfig

	// 关联的Raft节点 - 复用现有指标收集机制
	node NodeInterface

	// 指标存储
	snapshots   []LoadSnapshot                    // 历史快照
	keyAccess   map[string]*KeyAccessRecord       // 键访问统计
	nodeMetrics map[raft.NodeID]*raft.LoadMetrics // 集群节点指标

	// 统计计数器
	totalQueries    int64 // 总查询数
	totalOperations int64 // 总操作数

	// 控制
	ctx           context.Context
	cancel        context.CancelFunc
	collectTicker *time.Ticker
	cleanupTicker *time.Ticker

	// 日志
	logger *log.Logger
}

// NewLoadMonitor 创建负载监控代理 - 基于现有Raft节点
func NewLoadMonitor(config *LoadMonitorConfig, node NodeInterface) *LoadMonitor {
	if config == nil {
		config = DefaultLoadMonitorConfig()
	}

	ctx, cancel := context.WithCancel(context.Background())

	monitor := &LoadMonitor{
		config:      config,
		node:        node,
		snapshots:   make([]LoadSnapshot, 0, 1000),
		keyAccess:   make(map[string]*KeyAccessRecord),
		nodeMetrics: make(map[raft.NodeID]*raft.LoadMetrics),
		ctx:         ctx,
		cancel:      cancel,
		logger:      log.New(log.Writer(), "[LoadMonitor] ", log.LstdFlags),
	}

	return monitor
}

// Start 启动负载监控
func (lm *LoadMonitor) Start() error {
	lm.logger.Printf("启动负载监控代理，收集间隔: %v", lm.config.CollectionInterval)

	// 启动指标收集
	lm.collectTicker = time.NewTicker(lm.config.CollectionInterval)
	go lm.collectLoop()

	// 启动清理任务
	lm.cleanupTicker = time.NewTicker(lm.config.MetricsRetention / 10)
	go lm.cleanupLoop()

	// TODO: 后续可添加HTTP API服务器
	lm.logger.Printf("负载监控代理启动完成")

	return nil
}

// Stop 停止负载监控
func (lm *LoadMonitor) Stop() error {
	lm.logger.Printf("停止负载监控代理")

	// 取消上下文
	lm.cancel()

	// 停止定时器
	if lm.collectTicker != nil {
		lm.collectTicker.Stop()
	}
	if lm.cleanupTicker != nil {
		lm.cleanupTicker.Stop()
	}

	return nil
}

// collectLoop 指标收集循环 - 复用现有Node.GetMetrics()方法
func (lm *LoadMonitor) collectLoop() {
	for {
		select {
		case <-lm.ctx.Done():
			return
		case <-lm.collectTicker.C:
			lm.collectMetrics()
		}
	}
}

// collectMetrics 收集指标 - 基于现有Raft指标系统
func (lm *LoadMonitor) collectMetrics() {
	// 从现有Raft节点获取基础指标
	raftMetrics := lm.node.GetMetrics()
	if raftMetrics == nil {
		lm.logger.Printf("警告: 无法获取Raft指标")
		return
	}

	// 收集系统负载指标
	loadMetrics := lm.collectSystemMetrics()

	// 计算综合负载评分
	loadMetrics.LoadScore = lm.calculateLoadScore(loadMetrics)

	// 更新时间戳
	loadMetrics.LastUpdate = time.Now()

	// 创建负载快照
	snapshot := LoadSnapshot{
		NodeID:    lm.node.GetID(),
		Timestamp: time.Now(),
		Metrics:   loadMetrics,
	}

	// 存储快照
	lm.mu.Lock()
	lm.snapshots = append(lm.snapshots, snapshot)
	lm.nodeMetrics[lm.node.GetID()] = loadMetrics
	lm.mu.Unlock()

	// 更新Raft指标中的负载信息
	raftMetrics.Load = loadMetrics
	raftMetrics.CollectedAt = time.Now()

	lm.logger.Printf("收集负载指标完成 - QPS: %.2f, 存储: %.2fMB, 内存: %.2f%%, 负载评分: %.3f",
		loadMetrics.QPS, loadMetrics.StorageUsage, loadMetrics.MemoryUsage*100, loadMetrics.LoadScore)
}

// collectSystemMetrics 收集系统指标
func (lm *LoadMonitor) collectSystemMetrics() *raft.LoadMetrics {
	loadMetrics := &raft.LoadMetrics{}

	// QPS统计 - 基于计数器计算
	currentQueries := atomic.LoadInt64(&lm.totalQueries)
	currentOps := atomic.LoadInt64(&lm.totalOperations)

	// 简化QPS计算（实际实现中应该基于时间窗口）
	loadMetrics.QPS = float64(currentQueries+currentOps) / lm.config.CollectionInterval.Seconds()

	// 内存使用率
	var memStats runtime.MemStats
	runtime.ReadMemStats(&memStats)
	loadMetrics.MemoryUsage = float64(memStats.HeapInuse) / float64(memStats.Sys)

	// 存储使用量 (简化实现)
	loadMetrics.StorageUsage = float64(memStats.HeapInuse) / (1024 * 1024) // MB

	// CPU使用率 (简化实现，实际应该基于系统调用)
	loadMetrics.CPUUsage = math.Min(loadMetrics.QPS/1000.0, 1.0)

	// 网络IO (简化实现)
	loadMetrics.NetworkIOBytes = loadMetrics.QPS * 1024 // 假设每个操作1KB

	// 活跃连接数 (简化实现)
	loadMetrics.ActiveConnections = int64(runtime.NumGoroutine())

	// 待处理操作数
	loadMetrics.PendingOperations = currentOps % 100

	// 热点键检测
	loadMetrics.HotKeys = lm.getHotKeys()

	return loadMetrics
}

// calculateLoadScore 计算综合负载评分 (0.0-1.0)
func (lm *LoadMonitor) calculateLoadScore(metrics *raft.LoadMetrics) float64 {
	weights := lm.config.LoadScoreWeights

	// QPS评分 (归一化到0-1)
	qpsScore := math.Min(metrics.QPS/1000.0, 1.0)

	// 存储评分 (基于存储使用量，假设10GB为满载)
	storageScore := math.Min(metrics.StorageUsage/(10*1024), 1.0) // 10GB

	// 内存评分
	memoryScore := metrics.MemoryUsage

	// CPU评分
	cpuScore := metrics.CPUUsage

	// 网络评分 (假设100MB/s为满载)
	networkScore := math.Min(metrics.NetworkIOBytes/(100*1024*1024), 1.0)

	// 加权计算综合评分
	totalScore := weights.QPS*qpsScore +
		weights.Storage*storageScore +
		weights.Memory*memoryScore +
		weights.CPU*cpuScore +
		weights.Network*networkScore

	return math.Min(totalScore, 1.0)
}

// getHotKeys 获取热点键列表
func (lm *LoadMonitor) getHotKeys() []string {
	lm.mu.RLock()
	defer lm.mu.RUnlock()

	// 收集热点键
	type keyCount struct {
		key   string
		count int64
	}

	var hotKeys []keyCount
	cutoff := time.Now().Add(-lm.config.HotKeyWindowSize)

	for key, record := range lm.keyAccess {
		if record.LastAccess.After(cutoff) && float64(record.Count) > lm.config.HotKeyThreshold {
			hotKeys = append(hotKeys, keyCount{key: key, count: record.Count})
		}
	}

	// 按访问次数排序
	sort.Slice(hotKeys, func(i, j int) bool {
		return hotKeys[i].count > hotKeys[j].count
	})

	// 返回前10个热点键
	result := make([]string, 0, 10)
	for i, hk := range hotKeys {
		if i >= 10 {
			break
		}
		result = append(result, hk.key)
	}

	return result
}

// RecordKeyAccess 记录键访问 - 供外部调用
func (lm *LoadMonitor) RecordKeyAccess(key string) {
	atomic.AddInt64(&lm.totalQueries, 1)

	lm.mu.Lock()
	defer lm.mu.Unlock()

	if record, exists := lm.keyAccess[key]; exists {
		record.Count++
		record.LastAccess = time.Now()
	} else {
		lm.keyAccess[key] = &KeyAccessRecord{
			Key:        key,
			Count:      1,
			LastAccess: time.Now(),
		}
	}
}

// RecordOperation 记录操作 - 供外部调用
func (lm *LoadMonitor) RecordOperation() {
	atomic.AddInt64(&lm.totalOperations, 1)
}

// GetCurrentMetrics 获取当前负载指标
func (lm *LoadMonitor) GetCurrentMetrics() *raft.LoadMetrics {
	lm.mu.RLock()
	defer lm.mu.RUnlock()

	nodeID := lm.node.GetID()
	if metrics, exists := lm.nodeMetrics[nodeID]; exists {
		return metrics
	}
	return nil
}

// GetHistoricalSnapshots 获取历史快照
func (lm *LoadMonitor) GetHistoricalSnapshots(duration time.Duration) []LoadSnapshot {
	lm.mu.RLock()
	defer lm.mu.RUnlock()

	cutoff := time.Now().Add(-duration)
	var result []LoadSnapshot

	for _, snapshot := range lm.snapshots {
		if snapshot.Timestamp.After(cutoff) {
			result = append(result, snapshot)
		}
	}

	return result
}

// GetNodeMetrics 获取指定节点的负载指标
func (lm *LoadMonitor) GetNodeMetrics(nodeID raft.NodeID) *raft.LoadMetrics {
	lm.mu.RLock()
	defer lm.mu.RUnlock()

	return lm.nodeMetrics[nodeID]
}

// GetAllNodeMetrics 获取所有节点的负载指标
func (lm *LoadMonitor) GetAllNodeMetrics() map[raft.NodeID]*raft.LoadMetrics {
	lm.mu.RLock()
	defer lm.mu.RUnlock()

	result := make(map[raft.NodeID]*raft.LoadMetrics)
	for nodeID, metrics := range lm.nodeMetrics {
		result[nodeID] = metrics
	}
	return result
}

// cleanupLoop 清理过期数据
func (lm *LoadMonitor) cleanupLoop() {
	for {
		select {
		case <-lm.ctx.Done():
			return
		case <-lm.cleanupTicker.C:
			lm.cleanup()
		}
	}
}

// cleanup 清理过期数据
func (lm *LoadMonitor) cleanup() {
	lm.mu.Lock()
	defer lm.mu.Unlock()

	cutoff := time.Now().Add(-lm.config.MetricsRetention)

	// 清理过期快照
	var validSnapshots []LoadSnapshot
	for _, snapshot := range lm.snapshots {
		if snapshot.Timestamp.After(cutoff) {
			validSnapshots = append(validSnapshots, snapshot)
		}
	}
	lm.snapshots = validSnapshots

	// 清理过期键访问记录
	for key, record := range lm.keyAccess {
		if record.LastAccess.Before(cutoff) {
			delete(lm.keyAccess, key)
		}
	}

	lm.logger.Printf("清理完成 - 快照数: %d, 键记录数: %d", len(lm.snapshots), len(lm.keyAccess))
}

// ExportMetrics 导出指标数据 (JSON格式)
func (lm *LoadMonitor) ExportMetrics() ([]byte, error) {
	lm.mu.RLock()
	defer lm.mu.RUnlock()

	data := struct {
		NodeMetrics map[raft.NodeID]*raft.LoadMetrics `json:"nodeMetrics"`
		Snapshots   []LoadSnapshot                    `json:"snapshots"`
		KeyAccess   map[string]*KeyAccessRecord       `json:"keyAccess"`
		Timestamp   time.Time                         `json:"timestamp"`
	}{
		NodeMetrics: lm.nodeMetrics,
		Snapshots:   lm.snapshots[max(0, len(lm.snapshots)-100):], // 最近100个快照
		KeyAccess:   lm.keyAccess,
		Timestamp:   time.Now(),
	}

	return json.Marshal(data)
}

// max helper function
func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}
