/*
* @Author: Lzww0608
* @Date: 2025-6-21 21:20:58
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-21 21:21:01
* @Description: ConcordKV key router service for distributed sharding
 */

package sharding

import (
	"context"
	"fmt"
	"sync"
	"time"

	"raftserver/raft"
)

// RoutingStrategy 路由策略
type RoutingStrategy int

const (
	RoutingStrategyPrimary     RoutingStrategy = iota // 路由到主节点
	RoutingStrategyReplica                            // 路由到副本节点
	RoutingStrategyNearest                            // 路由到最近的节点
	RoutingStrategyLoadBalance                        // 负载均衡路由
)

func (rs RoutingStrategy) String() string {
	switch rs {
	case RoutingStrategyPrimary:
		return "Primary"
	case RoutingStrategyReplica:
		return "Replica"
	case RoutingStrategyNearest:
		return "Nearest"
	case RoutingStrategyLoadBalance:
		return "LoadBalance"
	default:
		return "Unknown"
	}
}

// RoutingRequest 路由请求
type RoutingRequest struct {
	Key      string          // 要路由的键
	Strategy RoutingStrategy // 路由策略
	ReadOnly bool            // 是否为只读操作
	Context  context.Context // 请求上下文
}

// RoutingResult 路由结果
type RoutingResult struct {
	PrimaryNode  raft.NodeID   // 主节点
	ReplicaNodes []raft.NodeID // 副本节点列表
	TargetNode   raft.NodeID   // 目标节点（根据策略选择）
	ShardInfo    *ShardInfo    // 分片信息
	Cached       bool          // 是否来自缓存
}

// KeyRouterConfig 键路由器配置
type KeyRouterConfig struct {
	CacheSize       int             // 路由表缓存大小
	CacheTTL        time.Duration   // 缓存TTL
	RefreshInterval time.Duration   // 刷新间隔
	EnableCache     bool            // 是否启用缓存
	DefaultStrategy RoutingStrategy // 默认路由策略
	MaxRetries      int             // 最大重试次数
	RetryInterval   time.Duration   // 重试间隔
}

// DefaultKeyRouterConfig 默认键路由器配置
func DefaultKeyRouterConfig() *KeyRouterConfig {
	return &KeyRouterConfig{
		CacheSize:       10000,
		CacheTTL:        5 * time.Minute,
		RefreshInterval: 30 * time.Second,
		EnableCache:     true,
		DefaultStrategy: RoutingStrategyPrimary,
		MaxRetries:      3,
		RetryInterval:   100 * time.Millisecond,
	}
}

// RouteTableEntry 路由表条目
type RouteTableEntry struct {
	Key        string        // 路由键（可能是键的前缀或哈希值）
	ShardID    string        // 分片ID
	Primary    raft.NodeID   // 主节点
	Replicas   []raft.NodeID // 副本节点
	UpdateTime time.Time     // 更新时间
	Version    int64         // 版本号
}

// KeyRouter 键路由服务
type KeyRouter struct {
	mu              sync.RWMutex
	config          *KeyRouterConfig
	metadataManager *ShardMetadataManager
	hashRing        *ConsistentHashRing
	routeTable      map[string]*RouteTableEntry // 路由表缓存
	stats           *KeyRouterStats
	lastRefresh     time.Time
	ctx             context.Context
	cancel          context.CancelFunc
	refreshTicker   *time.Ticker
}

// KeyRouterStats 键路由器统计信息
type KeyRouterStats struct {
	TotalRequests  int64     // 总请求数
	CacheHits      int64     // 缓存命中数
	CacheMisses    int64     // 缓存未命中数
	RoutingErrors  int64     // 路由错误数
	AverageLatency float64   // 平均延迟（毫秒）
	LastUpdate     time.Time // 最后更新时间
	RouteTableSize int       // 路由表大小
	RefreshCount   int64     // 刷新次数
}

// NewKeyRouter 创建新的键路由服务
func NewKeyRouter(config *KeyRouterConfig, metadataManager *ShardMetadataManager, hashRing *ConsistentHashRing) *KeyRouter {
	if config == nil {
		config = DefaultKeyRouterConfig()
	}

	ctx, cancel := context.WithCancel(context.Background())

	router := &KeyRouter{
		config:          config,
		metadataManager: metadataManager,
		hashRing:        hashRing,
		routeTable:      make(map[string]*RouteTableEntry),
		stats:           &KeyRouterStats{},
		ctx:             ctx,
		cancel:          cancel,
	}

	// 初始化路由表
	router.refreshRouteTable()

	// 启动定期刷新
	if config.RefreshInterval > 0 {
		router.refreshTicker = time.NewTicker(config.RefreshInterval)
		go router.refreshLoop()
	}

	return router
}

// RouteKey 路由单个键
func (kr *KeyRouter) RouteKey(req *RoutingRequest) (*RoutingResult, error) {
	start := time.Now()
	defer func() {
		kr.updateLatencyStats(time.Since(start))
	}()

	kr.mu.Lock()
	kr.stats.TotalRequests++
	kr.mu.Unlock()

	// 首先尝试从缓存获取
	if kr.config.EnableCache {
		if result := kr.getFromCache(req.Key); result != nil {
			kr.mu.Lock()
			kr.stats.CacheHits++
			kr.mu.Unlock()

			// 根据策略选择目标节点
			targetNode := kr.selectTargetNode(result, req.Strategy, req.ReadOnly)
			result.TargetNode = targetNode
			result.Cached = true

			return result, nil
		}
	}

	kr.mu.Lock()
	kr.stats.CacheMisses++
	kr.mu.Unlock()

	// 从元数据管理器获取分片信息
	shardInfo, err := kr.metadataManager.GetShardByKey(req.Key)
	if err != nil {
		kr.mu.Lock()
		kr.stats.RoutingErrors++
		kr.mu.Unlock()
		return nil, fmt.Errorf("获取分片信息失败: %w", err)
	}

	// 构建路由结果
	result := &RoutingResult{
		PrimaryNode:  shardInfo.Primary,
		ReplicaNodes: make([]raft.NodeID, len(shardInfo.Replicas)),
		ShardInfo:    shardInfo,
		Cached:       false,
	}
	copy(result.ReplicaNodes, shardInfo.Replicas)

	// 根据策略选择目标节点
	result.TargetNode = kr.selectTargetNode(result, req.Strategy, req.ReadOnly)

	// 添加到缓存
	if kr.config.EnableCache {
		kr.addToCache(req.Key, result)
	}

	return result, nil
}

// RouteBatch 批量路由多个键
func (kr *KeyRouter) RouteBatch(keys []string, strategy RoutingStrategy, readOnly bool) (map[string]*RoutingResult, error) {
	results := make(map[string]*RoutingResult)

	// 并发处理多个键的路由
	var wg sync.WaitGroup
	var mu sync.Mutex
	errChan := make(chan error, len(keys))

	for _, key := range keys {
		wg.Add(1)
		go func(k string) {
			defer wg.Done()

			req := &RoutingRequest{
				Key:      k,
				Strategy: strategy,
				ReadOnly: readOnly,
				Context:  context.Background(),
			}

			result, err := kr.RouteKey(req)
			if err != nil {
				errChan <- fmt.Errorf("路由键 %s 失败: %w", k, err)
				return
			}

			mu.Lock()
			results[k] = result
			mu.Unlock()
		}(key)
	}

	wg.Wait()
	close(errChan)

	// 检查是否有错误
	if len(errChan) > 0 {
		return results, <-errChan
	}

	return results, nil
}

// GetNodeKeys 获取节点负责的键范围（用于数据迁移）
func (kr *KeyRouter) GetNodeKeys(nodeID raft.NodeID) ([]ShardRange, error) {
	shardIDs := kr.metadataManager.GetNodeShards(nodeID)
	ranges := make([]ShardRange, 0, len(shardIDs))

	for _, shardID := range shardIDs {
		shard, err := kr.metadataManager.GetShard(shardID)
		if err != nil {
			continue
		}
		ranges = append(ranges, shard.Range)
	}

	return ranges, nil
}

// UpdateRouteTable 手动更新路由表
func (kr *KeyRouter) UpdateRouteTable() error {
	return kr.refreshRouteTable()
}

// GetStats 获取统计信息
func (kr *KeyRouter) GetStats() *KeyRouterStats {
	kr.mu.RLock()
	defer kr.mu.RUnlock()

	stats := *kr.stats
	stats.RouteTableSize = len(kr.routeTable)
	return &stats
}

// Close 关闭键路由服务
func (kr *KeyRouter) Close() error {
	kr.cancel()

	if kr.refreshTicker != nil {
		kr.refreshTicker.Stop()
	}

	return nil
}

// 内部方法

// selectTargetNode 根据策略选择目标节点
func (kr *KeyRouter) selectTargetNode(result *RoutingResult, strategy RoutingStrategy, readOnly bool) raft.NodeID {
	switch strategy {
	case RoutingStrategyPrimary:
		return result.PrimaryNode

	case RoutingStrategyReplica:
		if readOnly && len(result.ReplicaNodes) > 0 {
			// 简单轮询选择副本
			idx := int(kr.stats.TotalRequests) % len(result.ReplicaNodes)
			return result.ReplicaNodes[idx]
		}
		return result.PrimaryNode

	case RoutingStrategyNearest:
		// 简化实现：选择第一个可用节点
		// 实际实现中应该考虑网络延迟等因素
		if readOnly && len(result.ReplicaNodes) > 0 {
			return result.ReplicaNodes[0]
		}
		return result.PrimaryNode

	case RoutingStrategyLoadBalance:
		// 简化的负载均衡：在主节点和副本之间轮询
		allNodes := make([]raft.NodeID, 0, 1+len(result.ReplicaNodes))
		allNodes = append(allNodes, result.PrimaryNode)
		if readOnly {
			allNodes = append(allNodes, result.ReplicaNodes...)
		}

		if len(allNodes) > 0 {
			idx := int(kr.stats.TotalRequests) % len(allNodes)
			return allNodes[idx]
		}
		return result.PrimaryNode

	default:
		return result.PrimaryNode
	}
}

// getFromCache 从缓存获取路由信息
func (kr *KeyRouter) getFromCache(key string) *RoutingResult {
	kr.mu.RLock()
	defer kr.mu.RUnlock()

	entry, exists := kr.routeTable[key]
	if !exists {
		return nil
	}

	// 检查缓存是否过期
	if time.Since(entry.UpdateTime) > kr.config.CacheTTL {
		return nil
	}

	// 构建路由结果
	result := &RoutingResult{
		PrimaryNode:  entry.Primary,
		ReplicaNodes: make([]raft.NodeID, len(entry.Replicas)),
	}
	copy(result.ReplicaNodes, entry.Replicas)

	return result
}

// addToCache 添加到缓存
func (kr *KeyRouter) addToCache(key string, result *RoutingResult) {
	kr.mu.Lock()
	defer kr.mu.Unlock()

	// 检查缓存大小限制
	if len(kr.routeTable) >= kr.config.CacheSize {
		// 简单的LRU清理：删除最旧的条目
		kr.evictOldestCacheEntry()
	}

	entry := &RouteTableEntry{
		Key:        key,
		Primary:    result.PrimaryNode,
		Replicas:   make([]raft.NodeID, len(result.ReplicaNodes)),
		UpdateTime: time.Now(),
	}
	copy(entry.Replicas, result.ReplicaNodes)

	if result.ShardInfo != nil {
		entry.ShardID = result.ShardInfo.ID
		entry.Version = result.ShardInfo.Version
	}

	kr.routeTable[key] = entry
}

// evictOldestCacheEntry 清除最旧的缓存条目
func (kr *KeyRouter) evictOldestCacheEntry() {
	var oldestKey string
	var oldestTime time.Time
	first := true

	for key, entry := range kr.routeTable {
		if first || entry.UpdateTime.Before(oldestTime) {
			oldestKey = key
			oldestTime = entry.UpdateTime
			first = false
		}
	}

	if oldestKey != "" {
		delete(kr.routeTable, oldestKey)
	}
}

// refreshRouteTable 刷新路由表
func (kr *KeyRouter) refreshRouteTable() error {
	shards := kr.metadataManager.GetAllShards()

	kr.mu.Lock()
	defer kr.mu.Unlock()

	// 更新现有条目的版本信息
	for _, entry := range kr.routeTable {
		if shard, exists := shards[entry.ShardID]; exists {
			if shard.Version > entry.Version {
				// 更新条目信息
				entry.Primary = shard.Primary
				entry.Replicas = make([]raft.NodeID, len(shard.Replicas))
				copy(entry.Replicas, shard.Replicas)
				entry.Version = shard.Version
				entry.UpdateTime = time.Now()
			}
		}
	}

	kr.stats.RefreshCount++
	kr.lastRefresh = time.Now()

	return nil
}

// refreshLoop 定期刷新循环
func (kr *KeyRouter) refreshLoop() {
	for {
		select {
		case <-kr.ctx.Done():
			return
		case <-kr.refreshTicker.C:
			if err := kr.refreshRouteTable(); err != nil {
				// 记录错误但继续运行
				continue
			}
		}
	}
}

// updateLatencyStats 更新延迟统计
func (kr *KeyRouter) updateLatencyStats(latency time.Duration) {
	kr.mu.Lock()
	defer kr.mu.Unlock()

	latencyMs := float64(latency.Nanoseconds()) / 1000000.0

	// 简单的移动平均
	if kr.stats.AverageLatency == 0 {
		kr.stats.AverageLatency = latencyMs
	} else {
		kr.stats.AverageLatency = (kr.stats.AverageLatency*0.9 + latencyMs*0.1)
	}

	kr.stats.LastUpdate = time.Now()
}
