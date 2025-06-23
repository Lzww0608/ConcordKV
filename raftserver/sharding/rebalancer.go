/*
* @Author: Lzww0608
* @Date: 2025-6-23 17:19:53
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-23 17:19:53
* @Description: ConcordKV 自动重平衡调度器 - 基于现有分片管理实现智能负载均衡
 */

package sharding

import (
	"context"
	"log"
	"sort"
	"sync"
	"time"

	"raftserver/raft"
)

// RebalancerConfig 重平衡调度器配置
type RebalancerConfig struct {
	LoadImbalanceThreshold  float64
	CheckInterval           time.Duration
	MinRebalanceInterval    time.Duration
	MaxConcurrentMigrations int
	RebalanceStrategy       RebalanceStrategy
	DryRun                  bool
	EnableAutoRebalance     bool
}

// RebalanceStrategy 重平衡策略
type RebalanceStrategy int

const (
	StrategyMinimalDisruption RebalanceStrategy = iota
	StrategyLoadOptimal
	StrategyWeightBased
)

// LoadImbalanceInfo 负载不平衡信息
type LoadImbalanceInfo struct {
	OverloadedNodes   []raft.NodeID
	UnderloadedNodes  []raft.NodeID
	AverageLoad       float64
	MaxLoad           float64
	MinLoad           float64
	ImbalanceRatio    float64
	RequiresRebalance bool
}

// LoadMonitorInterface 负载监控接口
type LoadMonitorInterface interface {
	GetAllNodeMetrics() map[raft.NodeID]*raft.LoadMetrics
	GetNodeMetrics(nodeID raft.NodeID) *raft.LoadMetrics
}

// Rebalancer 自动重平衡调度器
type Rebalancer struct {
	mu              sync.RWMutex
	config          *RebalancerConfig
	metadataManager *ShardMetadataManager
	hashRing        *ConsistentHashRing
	keyRouter       *KeyRouter
	loadMonitor     LoadMonitorInterface
	ctx             context.Context
	cancel          context.CancelFunc
	checkTicker     *time.Ticker
	logger          *log.Logger
}

// DefaultRebalancerConfig 默认重平衡配置
func DefaultRebalancerConfig() *RebalancerConfig {
	return &RebalancerConfig{
		LoadImbalanceThreshold:  0.2,
		CheckInterval:           30 * time.Second,
		MinRebalanceInterval:    5 * time.Minute,
		MaxConcurrentMigrations: 2,
		RebalanceStrategy:       StrategyMinimalDisruption,
		DryRun:                  false,
		EnableAutoRebalance:     true,
	}
}

// NewRebalancer 创建重平衡调度器
func NewRebalancer(config *RebalancerConfig, metadataManager *ShardMetadataManager,
	hashRing *ConsistentHashRing, keyRouter *KeyRouter, loadMonitor LoadMonitorInterface) *Rebalancer {

	if config == nil {
		config = DefaultRebalancerConfig()
	}

	ctx, cancel := context.WithCancel(context.Background())

	return &Rebalancer{
		config:          config,
		metadataManager: metadataManager,
		hashRing:        hashRing,
		keyRouter:       keyRouter,
		loadMonitor:     loadMonitor,
		ctx:             ctx,
		cancel:          cancel,
		logger:          log.New(log.Writer(), "[Rebalancer] ", log.LstdFlags),
	}
}

// Start 启动重平衡调度器
func (r *Rebalancer) Start() error {
	r.logger.Printf("启动重平衡调度器，检查间隔: %v", r.config.CheckInterval)

	if r.config.EnableAutoRebalance {
		r.checkTicker = time.NewTicker(r.config.CheckInterval)
		go r.checkLoop()
	}

	return nil
}

// Stop 停止重平衡调度器
func (r *Rebalancer) Stop() error {
	r.logger.Printf("停止重平衡调度器")
	r.cancel()
	if r.checkTicker != nil {
		r.checkTicker.Stop()
	}
	return nil
}

// checkLoop 负载检查循环
func (r *Rebalancer) checkLoop() {
	for {
		select {
		case <-r.ctx.Done():
			return
		case <-r.checkTicker.C:
			r.checkAndRebalance()
		}
	}
}

// checkAndRebalance 检查负载并执行重平衡
func (r *Rebalancer) checkAndRebalance() {
	imbalanceInfo := r.detectLoadImbalance()
	if imbalanceInfo == nil || !imbalanceInfo.RequiresRebalance {
		return
	}

	r.logger.Printf("检测到负载不平衡 - 比率: %.3f", imbalanceInfo.ImbalanceRatio)

	// 在实际实现中，这里会生成和执行重平衡计划
	r.executeRebalance(imbalanceInfo)
}

// detectLoadImbalance 检测负载不平衡
func (r *Rebalancer) detectLoadImbalance() *LoadImbalanceInfo {
	nodeMetrics := r.loadMonitor.GetAllNodeMetrics()
	if len(nodeMetrics) == 0 {
		return nil
	}

	var totalLoad float64
	var loads []float64
	var nodeLoads = make(map[raft.NodeID]float64)

	for nodeID, metrics := range nodeMetrics {
		if metrics != nil {
			load := metrics.LoadScore
			totalLoad += load
			loads = append(loads, load)
			nodeLoads[nodeID] = load
		}
	}

	if len(loads) == 0 {
		return nil
	}

	averageLoad := totalLoad / float64(len(loads))
	sort.Float64s(loads)
	minLoad := loads[0]
	maxLoad := loads[len(loads)-1]

	imbalanceRatio := 0.0
	if averageLoad > 0 {
		imbalanceRatio = (maxLoad - minLoad) / averageLoad
	}

	requiresRebalance := imbalanceRatio > r.config.LoadImbalanceThreshold

	var overloadedNodes, underloadedNodes []raft.NodeID
	threshold := averageLoad * r.config.LoadImbalanceThreshold

	for nodeID, load := range nodeLoads {
		if load > averageLoad+threshold {
			overloadedNodes = append(overloadedNodes, nodeID)
		} else if load < averageLoad-threshold {
			underloadedNodes = append(underloadedNodes, nodeID)
		}
	}

	return &LoadImbalanceInfo{
		OverloadedNodes:   overloadedNodes,
		UnderloadedNodes:  underloadedNodes,
		AverageLoad:       averageLoad,
		MaxLoad:           maxLoad,
		MinLoad:           minLoad,
		ImbalanceRatio:    imbalanceRatio,
		RequiresRebalance: requiresRebalance,
	}
}

// executeRebalance 执行重平衡
func (r *Rebalancer) executeRebalance(info *LoadImbalanceInfo) {
	switch r.config.RebalanceStrategy {
	case StrategyWeightBased:
		r.executeWeightBasedRebalance(info)
	default:
		r.logger.Printf("重平衡策略 %v 暂未实现", r.config.RebalanceStrategy)
	}
}

// executeWeightBasedRebalance 执行基于权重的重平衡
func (r *Rebalancer) executeWeightBasedRebalance(info *LoadImbalanceInfo) {
	for _, nodeID := range info.OverloadedNodes {
		nodeMetrics := r.loadMonitor.GetNodeMetrics(nodeID)
		if nodeMetrics != nil {
			newWeight := 1.0 / (1.0 + nodeMetrics.LoadScore)
			err := r.hashRing.UpdateNodeWeight(nodeID, newWeight)
			if err != nil {
				r.logger.Printf("更新节点权重失败: %v", err)
			} else {
				r.logger.Printf("调整节点 %s 权重为 %.3f", nodeID, newWeight)
			}
		}
	}
}
