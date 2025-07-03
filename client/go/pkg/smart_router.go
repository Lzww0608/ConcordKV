/*
* @Author: Lzww0608
* @Date: 2025-7-2 22:23:48
* @LastEditors: Lzww0608
* @LastEditTime: 2025-7-2 22:23:50
* @Description: ConcordKV intelligent client - smart routing module
 */

package concord

import (
	"context"
	"crypto/md5"
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"sort"
	"sync"
	"sync/atomic"
	"time"
)

// RoutingStrategy 路由策略
type RoutingStrategy int

const (
	RoutingWritePrimary RoutingStrategy = iota // 写请求路由到主节点
	RoutingReadReplica                         // 读请求路由到副本节点
	RoutingReadNearest                         // 读请求路由到最近节点
	RoutingLoadBalance                         // 负载均衡路由
	RoutingFailover                            // 故障转移路由
)

func (rs RoutingStrategy) String() string {
	switch rs {
	case RoutingWritePrimary:
		return "WritePrimary"
	case RoutingReadReplica:
		return "ReadReplica"
	case RoutingReadNearest:
		return "ReadNearest"
	case RoutingLoadBalance:
		return "LoadBalance"
	case RoutingFailover:
		return "Failover"
	default:
		return "Unknown"
	}
}

// RoutingRequest 路由请求
type RoutingRequest struct {
	Key         string          `json:"key"`         // 要路由的键
	Strategy    RoutingStrategy `json:"strategy"`    // 路由策略
	ReadOnly    bool            `json:"readOnly"`    // 是否为只读操作
	PreferredDC string          `json:"preferredDC"` // 首选数据中心
	Timeout     time.Duration   `json:"timeout"`     // 请求超时
	Context     context.Context `json:"-"`           // 请求上下文
}

// RoutingResult 路由结果
type RoutingResult struct {
	PrimaryNode   NodeID          `json:"primaryNode"`   // 主节点
	ReplicaNodes  []NodeID        `json:"replicaNodes"`  // 副本节点列表
	TargetNode    NodeID          `json:"targetNode"`    // 目标节点（根据策略选择）
	BackupNodes   []NodeID        `json:"backupNodes"`   // 备用节点列表
	ShardInfo     *ShardInfo      `json:"shardInfo"`     // 分片信息
	Strategy      RoutingStrategy `json:"strategy"`      // 使用的策略
	Latency       time.Duration   `json:"latency"`       // 路由延迟
	Cached        bool            `json:"cached"`        // 是否来自缓存
	LoadBalanceID string          `json:"loadBalanceId"` // 负载均衡标识
}

// SmartRouterConfig 智能路由器配置
type SmartRouterConfig struct {
	// 基础配置
	DefaultStrategy RoutingStrategy `json:"defaultStrategy"` // 默认路由策略
	EnableCache     bool            `json:"enableCache"`     // 是否启用缓存
	CacheSize       int             `json:"cacheSize"`       // 缓存大小
	CacheTTL        time.Duration   `json:"cacheTTL"`        // 缓存TTL

	// 负载均衡配置
	LoadBalanceAlgorithm LoadBalanceAlgorithm `json:"loadBalanceAlgorithm"` // 负载均衡算法
	WeightEnabled        bool                 `json:"weightEnabled"`        // 是否启用权重
	StickySession        bool                 `json:"stickySession"`        // 是否启用粘性会话

	// 故障检测配置
	HealthCheckInterval time.Duration `json:"healthCheckInterval"` // 健康检查间隔
	FailureThreshold    int           `json:"failureThreshold"`    // 故障阈值
	RecoveryThreshold   int           `json:"recoveryThreshold"`   // 恢复阈值
	NodeTimeout         time.Duration `json:"nodeTimeout"`         // 节点超时时间

	// 重试配置
	MaxRetries         int           `json:"maxRetries"`         // 最大重试次数
	RetryInterval      time.Duration `json:"retryInterval"`      // 重试间隔
	BackoffMultiplier  float64       `json:"backoffMultiplier"`  // 退避倍数
	MaxBackoffInterval time.Duration `json:"maxBackoffInterval"` // 最大退避间隔
	EnableJitter       bool          `json:"enableJitter"`       // 是否启用抖动

	// 熔断器配置
	CircuitBreakerEnabled bool          `json:"circuitBreakerEnabled"` // 是否启用熔断器
	FailureRateThreshold  float64       `json:"failureRateThreshold"`  // 故障率阈值
	MinRequestThreshold   int           `json:"minRequestThreshold"`   // 最小请求阈值
	CircuitOpenTimeout    time.Duration `json:"circuitOpenTimeout"`    // 熔断器开启超时
	HalfOpenMaxCalls      int           `json:"halfOpenMaxCalls"`      // 半开状态最大调用数
}

// LoadBalanceAlgorithm 负载均衡算法
type LoadBalanceAlgorithm int

const (
	LBRoundRobin         LoadBalanceAlgorithm = iota // 轮询
	LBWeightedRoundRobin                             // 加权轮询
	LBLeastConnections                               // 最少连接数
	LBRandom                                         // 随机
	LBConsistentHash                                 // 一致性哈希
)

func (lba LoadBalanceAlgorithm) String() string {
	switch lba {
	case LBRoundRobin:
		return "RoundRobin"
	case LBWeightedRoundRobin:
		return "WeightedRoundRobin"
	case LBLeastConnections:
		return "LeastConnections"
	case LBRandom:
		return "Random"
	case LBConsistentHash:
		return "ConsistentHash"
	default:
		return "Unknown"
	}
}

// DefaultSmartRouterConfig 默认智能路由器配置
func DefaultSmartRouterConfig() *SmartRouterConfig {
	return &SmartRouterConfig{
		DefaultStrategy:       RoutingWritePrimary,
		EnableCache:           true,
		CacheSize:             10000,
		CacheTTL:              5 * time.Minute,
		LoadBalanceAlgorithm:  LBRoundRobin,
		WeightEnabled:         false,
		StickySession:         false,
		HealthCheckInterval:   30 * time.Second,
		FailureThreshold:      3,
		RecoveryThreshold:     2,
		NodeTimeout:           5 * time.Second,
		MaxRetries:            3,
		RetryInterval:         100 * time.Millisecond,
		BackoffMultiplier:     2.0,
		MaxBackoffInterval:    5 * time.Second,
		EnableJitter:          true,
		CircuitBreakerEnabled: true,
		FailureRateThreshold:  0.5,
		MinRequestThreshold:   10,
		CircuitOpenTimeout:    60 * time.Second,
		HalfOpenMaxCalls:      5,
	}
}

// NodeHealthStatus 节点健康状态
type NodeHealthStatus int

const (
	NodeHealthy     NodeHealthStatus = iota // 健康
	NodeUnhealthy                           // 不健康
	NodeRecovering                          // 恢复中
	NodeUnavailable                         // 不可用
)

func (nhs NodeHealthStatus) String() string {
	switch nhs {
	case NodeHealthy:
		return "Healthy"
	case NodeUnhealthy:
		return "Unhealthy"
	case NodeRecovering:
		return "Recovering"
	case NodeUnavailable:
		return "Unavailable"
	default:
		return "Unknown"
	}
}

// NodeHealth 节点健康信息
type NodeHealth struct {
	NodeID            NodeID           `json:"nodeId"`            // 节点ID
	Status            NodeHealthStatus `json:"status"`            // 健康状态
	LastCheckTime     time.Time        `json:"lastCheckTime"`     // 最后检查时间
	FailureCount      int              `json:"failureCount"`      // 连续失败次数
	SuccessCount      int              `json:"successCount"`      // 连续成功次数
	TotalRequests     int64            `json:"totalRequests"`     // 总请求数
	FailedRequests    int64            `json:"failedRequests"`    // 失败请求数
	AverageLatency    time.Duration    `json:"averageLatency"`    // 平均延迟
	Weight            int              `json:"weight"`            // 权重
	ActiveConnections int64            `json:"activeConnections"` // 活跃连接数
	LastError         string           `json:"lastError"`         // 最后错误信息
}

// CircuitBreakerState 熔断器状态
type CircuitBreakerState int

const (
	CircuitClosed   CircuitBreakerState = iota // 关闭状态
	CircuitOpen                                // 开启状态
	CircuitHalfOpen                            // 半开状态
)

func (cbs CircuitBreakerState) String() string {
	switch cbs {
	case CircuitClosed:
		return "Closed"
	case CircuitOpen:
		return "Open"
	case CircuitHalfOpen:
		return "HalfOpen"
	default:
		return "Unknown"
	}
}

// CircuitBreaker 熔断器
type CircuitBreaker struct {
	mu                sync.RWMutex
	state             CircuitBreakerState
	failureCount      int64
	successCount      int64
	requestCount      int64
	lastFailureTime   time.Time
	lastSuccessTime   time.Time
	config            *SmartRouterConfig
	halfOpenCallCount int64
}

// NewCircuitBreaker 创建新的熔断器
func NewCircuitBreaker(config *SmartRouterConfig) *CircuitBreaker {
	return &CircuitBreaker{
		state:  CircuitClosed,
		config: config,
	}
}

// Call 执行调用（通过熔断器）
func (cb *CircuitBreaker) Call(fn func() error) error {
	if !cb.AllowRequest() {
		return errors.New("熔断器开启，请求被拒绝")
	}

	start := time.Now()
	err := fn()
	latency := time.Since(start)

	if err != nil {
		cb.OnFailure(latency)
		return err
	}

	cb.OnSuccess(latency)
	return nil
}

// AllowRequest 检查是否允许请求
func (cb *CircuitBreaker) AllowRequest() bool {
	cb.mu.Lock()
	defer cb.mu.Unlock()

	switch cb.state {
	case CircuitClosed:
		return true
	case CircuitOpen:
		// 检查是否可以进入半开状态
		if time.Since(cb.lastFailureTime) > cb.config.CircuitOpenTimeout {
			cb.state = CircuitHalfOpen
			cb.halfOpenCallCount = 0
			return true
		}
		return false
	case CircuitHalfOpen:
		// 半开状态下限制调用数量
		return cb.halfOpenCallCount < int64(cb.config.HalfOpenMaxCalls)
	default:
		return false
	}
}

// OnSuccess 记录成功调用
func (cb *CircuitBreaker) OnSuccess(latency time.Duration) {
	cb.mu.Lock()
	defer cb.mu.Unlock()

	atomic.AddInt64(&cb.successCount, 1)
	atomic.AddInt64(&cb.requestCount, 1)
	cb.lastSuccessTime = time.Now()

	if cb.state == CircuitHalfOpen {
		cb.halfOpenCallCount++
		if cb.halfOpenCallCount >= int64(cb.config.HalfOpenMaxCalls) {
			cb.state = CircuitClosed
			cb.failureCount = 0
		}
	}
}

// OnFailure 记录失败调用
func (cb *CircuitBreaker) OnFailure(latency time.Duration) {
	cb.mu.Lock()
	defer cb.mu.Unlock()

	atomic.AddInt64(&cb.failureCount, 1)
	atomic.AddInt64(&cb.requestCount, 1)
	cb.lastFailureTime = time.Now()

	if cb.state == CircuitHalfOpen {
		cb.state = CircuitOpen
		return
	}

	if cb.shouldTrip() {
		cb.state = CircuitOpen
	}
}

// shouldTrip 检查是否应该触发熔断
func (cb *CircuitBreaker) shouldTrip() bool {
	requestCount := atomic.LoadInt64(&cb.requestCount)
	failureCount := atomic.LoadInt64(&cb.failureCount)

	if requestCount < int64(cb.config.MinRequestThreshold) {
		return false
	}

	failureRate := float64(failureCount) / float64(requestCount)
	return failureRate >= cb.config.FailureRateThreshold
}

// GetState 获取熔断器状态
func (cb *CircuitBreaker) GetState() CircuitBreakerState {
	cb.mu.RLock()
	defer cb.mu.RUnlock()
	return cb.state
}

// SmartRouter 智能路由器
type SmartRouter struct {
	mu                 sync.RWMutex
	config             *SmartRouterConfig
	topologyCache      *TopologyCache
	nodeHealthMap      map[NodeID]*NodeHealth     // 节点健康状态映射
	circuitBreakers    map[NodeID]*CircuitBreaker // 节点熔断器映射
	routeCache         map[string]*RoutingResult  // 路由结果缓存
	loadBalancer       LoadBalancer               // 负载均衡器
	consistentHashRing *ConsistentHashRing        // 一致性哈希环
	stats              *SmartRouterStats          // 统计信息
	stopChannel        chan struct{}              // 停止信号
	isRunning          int64                      // 运行状态
}

// LoadBalancer 负载均衡器接口
type LoadBalancer interface {
	Select(nodes []NodeID, key string) (NodeID, error)
	UpdateWeight(nodeID NodeID, weight int)
	GetStats() map[NodeID]int64
}

// RoundRobinLoadBalancer 轮询负载均衡器
type RoundRobinLoadBalancer struct {
	mu      sync.Mutex
	counter int64
	weights map[NodeID]int
	stats   map[NodeID]int64
}

// NewRoundRobinLoadBalancer 创建轮询负载均衡器
func NewRoundRobinLoadBalancer() *RoundRobinLoadBalancer {
	return &RoundRobinLoadBalancer{
		weights: make(map[NodeID]int),
		stats:   make(map[NodeID]int64),
	}
}

// Select 选择节点
func (rrlb *RoundRobinLoadBalancer) Select(nodes []NodeID, key string) (NodeID, error) {
	if len(nodes) == 0 {
		return "", errors.New("没有可用节点")
	}

	rrlb.mu.Lock()
	defer rrlb.mu.Unlock()

	// 简单轮询实现
	index := atomic.AddInt64(&rrlb.counter, 1) % int64(len(nodes))
	selectedNode := nodes[index]

	// 更新统计
	rrlb.stats[selectedNode]++

	return selectedNode, nil
}

// UpdateWeight 更新权重
func (rrlb *RoundRobinLoadBalancer) UpdateWeight(nodeID NodeID, weight int) {
	rrlb.mu.Lock()
	defer rrlb.mu.Unlock()
	rrlb.weights[nodeID] = weight
}

// GetStats 获取统计信息
func (rrlb *RoundRobinLoadBalancer) GetStats() map[NodeID]int64 {
	rrlb.mu.Lock()
	defer rrlb.mu.Unlock()

	statsCopy := make(map[NodeID]int64)
	for k, v := range rrlb.stats {
		statsCopy[k] = v
	}
	return statsCopy
}

// ConsistentHashRing 一致性哈希环 (复用raftserver实现)
type ConsistentHashRing struct {
	mu           sync.RWMutex
	ring         map[uint64]NodeID   // 哈希环
	sortedHashes []uint64            // 排序的哈希值
	virtualNodes int                 // 虚拟节点数
	hashFunc     func(string) uint64 // 哈希函数
}

// NewConsistentHashRing 创建一致性哈希环
func NewConsistentHashRing(virtualNodes int) *ConsistentHashRing {
	return &ConsistentHashRing{
		ring:         make(map[uint64]NodeID),
		virtualNodes: virtualNodes,
		hashFunc:     md5Hash,
	}
}

// AddNode 添加节点
func (chr *ConsistentHashRing) AddNode(nodeID NodeID) {
	chr.mu.Lock()
	defer chr.mu.Unlock()

	for i := 0; i < chr.virtualNodes; i++ {
		virtualKey := fmt.Sprintf("%s#%d", nodeID, i)
		hash := chr.hashFunc(virtualKey)
		chr.ring[hash] = nodeID
	}

	chr.updateSortedHashes()
}

// RemoveNode 移除节点
func (chr *ConsistentHashRing) RemoveNode(nodeID NodeID) {
	chr.mu.Lock()
	defer chr.mu.Unlock()

	for i := 0; i < chr.virtualNodes; i++ {
		virtualKey := fmt.Sprintf("%s#%d", nodeID, i)
		hash := chr.hashFunc(virtualKey)
		delete(chr.ring, hash)
	}

	chr.updateSortedHashes()
}

// GetNode 获取键对应的节点
func (chr *ConsistentHashRing) GetNode(key string) (NodeID, error) {
	chr.mu.RLock()
	defer chr.mu.RUnlock()

	if len(chr.ring) == 0 {
		return "", errors.New("哈希环为空")
	}

	hash := chr.hashFunc(key)

	// 使用二分查找找到第一个大于等于hash的节点
	idx := sort.Search(len(chr.sortedHashes), func(i int) bool {
		return chr.sortedHashes[i] >= hash
	})

	if idx == len(chr.sortedHashes) {
		idx = 0 // 环形，回到开头
	}

	return chr.ring[chr.sortedHashes[idx]], nil
}

// updateSortedHashes 更新排序的哈希值切片
func (chr *ConsistentHashRing) updateSortedHashes() {
	chr.sortedHashes = make([]uint64, 0, len(chr.ring))
	for hash := range chr.ring {
		chr.sortedHashes = append(chr.sortedHashes, hash)
	}
	sort.Slice(chr.sortedHashes, func(i, j int) bool {
		return chr.sortedHashes[i] < chr.sortedHashes[j]
	})
}

// md5Hash MD5哈希函数
func md5Hash(key string) uint64 {
	hasher := md5.New()
	hasher.Write([]byte(key))
	hash := hasher.Sum(nil)
	return binary.BigEndian.Uint64(hash[:8])
}

// SmartRouterStats 智能路由器统计信息
type SmartRouterStats struct {
	TotalRequests       int64                          `json:"totalRequests"`       // 总请求数
	SuccessfulRequests  int64                          `json:"successfulRequests"`  // 成功请求数
	FailedRequests      int64                          `json:"failedRequests"`      // 失败请求数
	CacheHits           int64                          `json:"cacheHits"`           // 缓存命中数
	CacheMisses         int64                          `json:"cacheMisses"`         // 缓存未命中数
	AverageLatency      time.Duration                  `json:"averageLatency"`      // 平均延迟
	NodeStats           map[NodeID]*NodeHealth         `json:"nodeStats"`           // 节点统计
	StrategyStats       map[RoutingStrategy]int64      `json:"strategyStats"`       // 策略统计
	CircuitBreakerStats map[NodeID]CircuitBreakerState `json:"circuitBreakerStats"` // 熔断器统计
	LastUpdate          time.Time                      `json:"lastUpdate"`          // 最后更新时间
}

// NewSmartRouter 创建新的智能路由器
func NewSmartRouter(config *SmartRouterConfig, topologyCache *TopologyCache) *SmartRouter {
	if config == nil {
		config = DefaultSmartRouterConfig()
	}

	sr := &SmartRouter{
		config:             config,
		topologyCache:      topologyCache,
		nodeHealthMap:      make(map[NodeID]*NodeHealth),
		circuitBreakers:    make(map[NodeID]*CircuitBreaker),
		routeCache:         make(map[string]*RoutingResult),
		consistentHashRing: NewConsistentHashRing(100), // 100个虚拟节点
		stopChannel:        make(chan struct{}),
		stats: &SmartRouterStats{
			NodeStats:           make(map[NodeID]*NodeHealth),
			StrategyStats:       make(map[RoutingStrategy]int64),
			CircuitBreakerStats: make(map[NodeID]CircuitBreakerState),
		},
	}

	// 创建负载均衡器
	switch config.LoadBalanceAlgorithm {
	case LBRoundRobin, LBWeightedRoundRobin:
		sr.loadBalancer = NewRoundRobinLoadBalancer()
	default:
		sr.loadBalancer = NewRoundRobinLoadBalancer()
	}

	return sr
}

// Start 启动智能路由器
func (sr *SmartRouter) Start(ctx context.Context) error {
	if !atomic.CompareAndSwapInt64(&sr.isRunning, 0, 1) {
		return errors.New("智能路由器已经在运行")
	}

	// 启动健康检查
	if sr.config.HealthCheckInterval > 0 {
		go sr.healthCheckLoop(ctx)
	}

	return nil
}

// Stop 停止智能路由器
func (sr *SmartRouter) Stop() {
	if atomic.CompareAndSwapInt64(&sr.isRunning, 1, 0) {
		close(sr.stopChannel)
	}
}

// Route 执行路由
func (sr *SmartRouter) Route(req *RoutingRequest) (*RoutingResult, error) {
	start := time.Now()
	defer func() {
		latency := time.Since(start)
		atomic.AddInt64(&sr.stats.TotalRequests, 1)
		sr.updateAverageLatency(latency)
		sr.stats.StrategyStats[req.Strategy]++
	}()

	// 检查缓存
	if sr.config.EnableCache {
		cacheKey := sr.generateCacheKey(req)
		if cachedResult, ok := sr.getFromCache(cacheKey); ok {
			cachedResult.Latency = time.Since(start)
			cachedResult.Cached = true
			atomic.AddInt64(&sr.stats.CacheHits, 1)
			return cachedResult, nil
		}
		atomic.AddInt64(&sr.stats.CacheMisses, 1)
	}

	// 获取分片信息
	shardInfo, err := sr.topologyCache.GetByKey(req.Key)
	if err != nil || shardInfo == nil {
		return nil, fmt.Errorf("获取分片信息失败: %w", err)
	}

	// 执行路由逻辑
	result := &RoutingResult{
		PrimaryNode:  shardInfo.Primary,
		ReplicaNodes: make([]NodeID, len(shardInfo.Replicas)),
		ShardInfo:    shardInfo,
		Strategy:     req.Strategy,
		Latency:      time.Since(start),
		Cached:       false,
	}
	copy(result.ReplicaNodes, shardInfo.Replicas)

	// 根据策略选择目标节点
	targetNode, backupNodes, err := sr.selectTargetNode(result, req)
	if err != nil {
		atomic.AddInt64(&sr.stats.FailedRequests, 1)
		return nil, err
	}

	result.TargetNode = targetNode
	result.BackupNodes = backupNodes

	// 缓存结果
	if sr.config.EnableCache {
		cacheKey := sr.generateCacheKey(req)
		sr.addToCache(cacheKey, result)
	}

	atomic.AddInt64(&sr.stats.SuccessfulRequests, 1)
	return result, nil
}

// RouteBatch 批量路由
func (sr *SmartRouter) RouteBatch(requests []*RoutingRequest) (map[string]*RoutingResult, error) {
	results := make(map[string]*RoutingResult)
	errors := make(map[string]error)

	// 并发处理批量请求
	var wg sync.WaitGroup
	resultChan := make(chan struct {
		key    string
		result *RoutingResult
		err    error
	}, len(requests))

	for _, req := range requests {
		wg.Add(1)
		go func(r *RoutingRequest) {
			defer wg.Done()
			result, err := sr.Route(r)
			resultChan <- struct {
				key    string
				result *RoutingResult
				err    error
			}{r.Key, result, err}
		}(req)
	}

	go func() {
		wg.Wait()
		close(resultChan)
	}()

	// 收集结果
	for item := range resultChan {
		if item.err != nil {
			errors[item.key] = item.err
		} else {
			results[item.key] = item.result
		}
	}

	if len(errors) > 0 {
		return results, fmt.Errorf("批量路由有%d个失败", len(errors))
	}

	return results, nil
}

// GetStats 获取统计信息
func (sr *SmartRouter) GetStats() *SmartRouterStats {
	sr.mu.RLock()
	defer sr.mu.RUnlock()

	// 深拷贝统计信息
	statsCopy := &SmartRouterStats{
		TotalRequests:       atomic.LoadInt64(&sr.stats.TotalRequests),
		SuccessfulRequests:  atomic.LoadInt64(&sr.stats.SuccessfulRequests),
		FailedRequests:      atomic.LoadInt64(&sr.stats.FailedRequests),
		CacheHits:           atomic.LoadInt64(&sr.stats.CacheHits),
		CacheMisses:         atomic.LoadInt64(&sr.stats.CacheMisses),
		AverageLatency:      sr.stats.AverageLatency,
		NodeStats:           make(map[NodeID]*NodeHealth),
		StrategyStats:       make(map[RoutingStrategy]int64),
		CircuitBreakerStats: make(map[NodeID]CircuitBreakerState),
		LastUpdate:          time.Now(),
	}

	// 复制节点统计
	for k, v := range sr.nodeHealthMap {
		healthCopy := *v
		statsCopy.NodeStats[k] = &healthCopy
	}

	// 复制策略统计
	for k, v := range sr.stats.StrategyStats {
		statsCopy.StrategyStats[k] = v
	}

	// 复制熔断器统计
	for k, v := range sr.circuitBreakers {
		statsCopy.CircuitBreakerStats[k] = v.GetState()
	}

	return statsCopy
}

// UpdateNodeHealth 更新节点健康状态
func (sr *SmartRouter) UpdateNodeHealth(nodeID NodeID, isHealthy bool, latency time.Duration, err error) {
	sr.mu.Lock()
	defer sr.mu.Unlock()

	health, exists := sr.nodeHealthMap[nodeID]
	if !exists {
		health = &NodeHealth{
			NodeID:        nodeID,
			Status:        NodeHealthy,
			Weight:        1,
			LastCheckTime: time.Now(),
		}
		sr.nodeHealthMap[nodeID] = health
	}

	health.LastCheckTime = time.Now()
	health.TotalRequests++

	if isHealthy {
		health.SuccessCount++
		health.FailureCount = 0
		if health.Status == NodeUnhealthy || health.Status == NodeRecovering {
			if health.SuccessCount >= sr.config.RecoveryThreshold {
				health.Status = NodeHealthy
			} else {
				health.Status = NodeRecovering
			}
		}
	} else {
		health.FailedRequests++
		health.FailureCount++
		health.SuccessCount = 0
		if err != nil {
			health.LastError = err.Error()
		}

		if health.FailureCount >= sr.config.FailureThreshold {
			health.Status = NodeUnhealthy
		}
	}

	// 更新平均延迟
	if latency > 0 {
		if health.AverageLatency == 0 {
			health.AverageLatency = latency
		} else {
			// 指数移动平均
			health.AverageLatency = time.Duration(float64(health.AverageLatency)*0.9 + float64(latency)*0.1)
		}
	}
}

// 内部方法：选择目标节点
func (sr *SmartRouter) selectTargetNode(result *RoutingResult, req *RoutingRequest) (NodeID, []NodeID, error) {
	allNodes := append([]NodeID{result.PrimaryNode}, result.ReplicaNodes...)
	availableNodes := sr.filterHealthyNodes(allNodes)

	if len(availableNodes) == 0 {
		return "", nil, errors.New("没有可用的健康节点")
	}

	var targetNode NodeID
	var err error

	switch req.Strategy {
	case RoutingWritePrimary:
		// 写请求必须路由到主节点
		if sr.isNodeHealthy(result.PrimaryNode) {
			targetNode = result.PrimaryNode
		} else {
			return "", nil, errors.New("主节点不可用")
		}

	case RoutingReadReplica:
		// 读请求优先路由到副本节点
		healthyReplicas := sr.filterHealthyNodes(result.ReplicaNodes)
		if len(healthyReplicas) > 0 {
			targetNode, err = sr.loadBalancer.Select(healthyReplicas, req.Key)
		} else if sr.isNodeHealthy(result.PrimaryNode) {
			targetNode = result.PrimaryNode
		} else {
			return "", nil, errors.New("没有可用的副本节点")
		}

	case RoutingReadNearest:
		// 路由到延迟最低的节点
		targetNode = sr.selectNearestNode(availableNodes)

	case RoutingLoadBalance:
		// 负载均衡选择
		targetNode, err = sr.loadBalancer.Select(availableNodes, req.Key)

	case RoutingFailover:
		// 故障转移，尝试主节点，失败则选择副本
		if sr.isNodeHealthy(result.PrimaryNode) {
			targetNode = result.PrimaryNode
		} else {
			healthyReplicas := sr.filterHealthyNodes(result.ReplicaNodes)
			if len(healthyReplicas) > 0 {
				targetNode, err = sr.loadBalancer.Select(healthyReplicas, req.Key)
			} else {
				return "", nil, errors.New("所有节点都不可用")
			}
		}

	default:
		targetNode, err = sr.loadBalancer.Select(availableNodes, req.Key)
	}

	if err != nil {
		return "", nil, err
	}

	// 生成备用节点列表
	backupNodes := make([]NodeID, 0)
	for _, node := range availableNodes {
		if node != targetNode {
			backupNodes = append(backupNodes, node)
		}
	}

	return targetNode, backupNodes, nil
}

// 内部方法：过滤健康节点
func (sr *SmartRouter) filterHealthyNodes(nodes []NodeID) []NodeID {
	sr.mu.RLock()
	defer sr.mu.RUnlock()

	healthyNodes := make([]NodeID, 0, len(nodes))
	for _, node := range nodes {
		if sr.isNodeHealthy(node) {
			healthyNodes = append(healthyNodes, node)
		}
	}
	return healthyNodes
}

// 内部方法：检查节点是否健康
func (sr *SmartRouter) isNodeHealthy(nodeID NodeID) bool {
	health, exists := sr.nodeHealthMap[nodeID]
	if !exists {
		return true // 未知节点默认为健康
	}

	// 检查熔断器状态
	if cb, exists := sr.circuitBreakers[nodeID]; exists {
		if cb.GetState() == CircuitOpen {
			return false
		}
	}

	return health.Status == NodeHealthy || health.Status == NodeRecovering
}

// 内部方法：选择最近的节点
func (sr *SmartRouter) selectNearestNode(nodes []NodeID) NodeID {
	if len(nodes) == 0 {
		return ""
	}

	sr.mu.RLock()
	defer sr.mu.RUnlock()

	nearestNode := nodes[0]
	minLatency := time.Duration(math.MaxInt64)

	for _, node := range nodes {
		if health, exists := sr.nodeHealthMap[node]; exists {
			if health.AverageLatency < minLatency {
				minLatency = health.AverageLatency
				nearestNode = node
			}
		}
	}

	return nearestNode
}

// 内部方法：生成缓存键
func (sr *SmartRouter) generateCacheKey(req *RoutingRequest) string {
	return fmt.Sprintf("%s:%s:%t", req.Key, req.Strategy.String(), req.ReadOnly)
}

// 内部方法：从缓存获取
func (sr *SmartRouter) getFromCache(key string) (*RoutingResult, bool) {
	sr.mu.RLock()
	defer sr.mu.RUnlock()

	result, exists := sr.routeCache[key]
	if !exists {
		return nil, false
	}

	// 检查TTL
	if time.Since(result.Latency) > sr.config.CacheTTL {
		return nil, false
	}

	// 返回副本
	resultCopy := *result
	return &resultCopy, true
}

// 内部方法：添加到缓存
func (sr *SmartRouter) addToCache(key string, result *RoutingResult) {
	sr.mu.Lock()
	defer sr.mu.Unlock()

	// 检查缓存大小限制
	if len(sr.routeCache) >= sr.config.CacheSize {
		// 简单清理：删除一半的缓存
		for k := range sr.routeCache {
			delete(sr.routeCache, k)
			if len(sr.routeCache) <= sr.config.CacheSize/2 {
				break
			}
		}
	}

	// 添加到缓存
	resultCopy := *result
	resultCopy.Latency = time.Now().Sub(time.Now()) // 记录缓存时间
	sr.routeCache[key] = &resultCopy
}

// 内部方法：健康检查循环
func (sr *SmartRouter) healthCheckLoop(ctx context.Context) {
	ticker := time.NewTicker(sr.config.HealthCheckInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-sr.stopChannel:
			return
		case <-ticker.C:
			sr.performHealthCheck()
		}
	}
}

// 内部方法：执行健康检查
func (sr *SmartRouter) performHealthCheck() {
	sr.mu.RLock()
	nodes := make([]NodeID, 0, len(sr.nodeHealthMap))
	for nodeID := range sr.nodeHealthMap {
		nodes = append(nodes, nodeID)
	}
	sr.mu.RUnlock()

	// 并发检查所有节点
	var wg sync.WaitGroup
	for _, nodeID := range nodes {
		wg.Add(1)
		go func(nid NodeID) {
			defer wg.Done()
			sr.checkNodeHealth(nid)
		}(nodeID)
	}
	wg.Wait()
}

// 内部方法：检查单个节点健康状态
func (sr *SmartRouter) checkNodeHealth(nodeID NodeID) {
	start := time.Now()

	// TODO: 实现实际的健康检查逻辑
	// 这里应该发送ping请求或健康检查请求到节点

	// 模拟健康检查
	latency := time.Since(start)
	isHealthy := true // 暂时默认为健康

	sr.UpdateNodeHealth(nodeID, isHealthy, latency, nil)
}

// 内部方法：更新平均延迟
func (sr *SmartRouter) updateAverageLatency(latency time.Duration) {
	if sr.stats.AverageLatency == 0 {
		sr.stats.AverageLatency = latency
	} else {
		// 指数移动平均
		sr.stats.AverageLatency = time.Duration(float64(sr.stats.AverageLatency)*0.9 + float64(latency)*0.1)
	}
}
