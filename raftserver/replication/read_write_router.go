/*
 * @Author: Lzww0608
 * @Date: 2025-6-28 11:51:08
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-28 11:51:11
 * @Description: ConcordKV 读写分离路由器 - 智能请求路由系统
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

// ReadWriteRouterConfig 读写分离路由配置
type ReadWriteRouterConfig struct {
	// 基础配置
	Enabled bool `json:"enabled"`

	// 读路由策略
	ReadRoutingStrategy   RoutingStrategy `json:"readRoutingStrategy"`
	PreferLocalDC         bool            `json:"preferLocalDC"`
	MaxReadLatencyMs      int             `json:"maxReadLatencyMs"`
	ReadReplicaCount      int             `json:"readReplicaCount"`
	EnableReadReplication bool            `json:"enableReadReplication"`

	// 写路由策略
	WriteRoutingStrategy  RoutingStrategy   `json:"writeRoutingStrategy"`
	PrimaryDC             raft.DataCenterID `json:"primaryDC"`
	WriteConsistencyLevel ConsistencyLevel  `json:"writeConsistencyLevel"`

	// 负载均衡配置
	LoadBalancingMethod   LoadBalancingMethod `json:"loadBalancingMethod"`
	HealthCheckIntervalMs int                 `json:"healthCheckIntervalMs"`
	RetryAttempts         int                 `json:"retryAttempts"`
	RetryTimeoutMs        int                 `json:"retryTimeoutMs"`

	// 一致性配置
	ReadConsistency       ReadConsistencyLevel `json:"readConsistency"`
	StaleReadThresholdMs  int                  `json:"staleReadThresholdMs"`
	EnableLinearizability bool                 `json:"enableLinearizability"`

	// 监控配置
	EnableMetrics     bool `json:"enableMetrics"`
	MetricsIntervalMs int  `json:"metricsIntervalMs"`
}

// DefaultReadWriteRouterConfig 默认读写分离路由配置
func DefaultReadWriteRouterConfig() *ReadWriteRouterConfig {
	return &ReadWriteRouterConfig{
		Enabled:               true,
		ReadRoutingStrategy:   RoutingNearestDC,
		PreferLocalDC:         true,
		MaxReadLatencyMs:      100,
		ReadReplicaCount:      3,
		EnableReadReplication: true,
		WriteRoutingStrategy:  RoutingPrimaryDC,
		WriteConsistencyLevel: ConsistencyStrong,
		LoadBalancingMethod:   LoadBalanceRoundRobin,
		HealthCheckIntervalMs: 1000,
		RetryAttempts:         3,
		RetryTimeoutMs:        1000,
		ReadConsistency:       ReadConsistencyEventual,
		StaleReadThresholdMs:  5000,
		EnableLinearizability: false,
		EnableMetrics:         true,
		MetricsIntervalMs:     5000,
	}
}

// 枚举定义
type RoutingStrategy int
type ConsistencyLevel int
type LoadBalancingMethod int
type ReadConsistencyLevel int
type RequestType int

const (
	// 路由策略
	RoutingNearestDC RoutingStrategy = iota
	RoutingRoundRobin
	RoutingWeightedRoundRobin
	RoutingLeastLatency
	RoutingPrimaryDC
	RoutingLocalFirst

	// 一致性级别
	ConsistencyEventual ConsistencyLevel = iota
	ConsistencyStrong
	ConsistencyLinearizable

	// 负载均衡方法
	LoadBalanceRoundRobin LoadBalancingMethod = iota
	LoadBalanceWeighted
	LoadBalanceLeastConnections
	LoadBalanceLeastLatency

	// 读一致性级别
	ReadConsistencyEventual ReadConsistencyLevel = iota
	ReadConsistencyBounded
	ReadConsistencyStrong
	ReadConsistencyLinearizable

	// 请求类型
	RequestTypeRead RequestType = iota
	RequestTypeWrite
	RequestTypeReadWrite
)

// ReadWriteRouter 读写分离路由器
type ReadWriteRouter struct {
	mu sync.RWMutex

	// 基础配置
	nodeID     raft.NodeID
	config     *ReadWriteRouterConfig
	raftConfig *raft.Config
	logger     *log.Logger

	// 数据中心管理
	localDC      raft.DataCenterID
	primaryDC    raft.DataCenterID
	dataCenters  map[raft.DataCenterID]*DataCenterInfo
	readReplicas map[raft.DataCenterID][]raft.NodeID
	writeTargets map[raft.DataCenterID][]raft.NodeID

	// 路由状态
	routingTable  *RoutingTable
	loadBalancer  *LoadBalancer
	healthChecker *HealthChecker

	// 监控统计
	metrics *RouterMetrics

	// 控制流
	ctx     context.Context
	cancel  context.CancelFunc
	wg      sync.WaitGroup
	running bool
	stopCh  chan struct{}
}

// DataCenterInfo 数据中心信息
type DataCenterInfo struct {
	mu sync.RWMutex

	// 基本信息
	ID        raft.DataCenterID
	Region    string
	Nodes     []raft.NodeID
	IsPrimary bool
	IsLocal   bool

	// 网络信息
	Latency      time.Duration
	Bandwidth    int64
	IsHealthy    bool
	LastPing     time.Time
	FailureCount int64

	// 负载信息
	ActiveConnections int64
	RequestsPerSecond float64
	CPUUsage          float64
	MemoryUsage       float64

	// 复制状态
	ReplicationLag   time.Duration
	LastSyncTime     time.Time
	ConsistencyLevel ConsistencyLevel
}

// RoutingTable 路由表
type RoutingTable struct {
	mu sync.RWMutex

	// 路由规则
	readRoutes  map[string]*Route
	writeRoutes map[string]*Route

	// 默认路由
	defaultReadRoute  *Route
	defaultWriteRoute *Route

	// 路由统计
	routeStats map[string]*RouteStats
}

// Route 路由定义
type Route struct {
	// 路由信息
	ID       string
	Type     RequestType
	Pattern  string
	Priority int

	// 目标信息
	TargetDCs   []raft.DataCenterID
	TargetNodes []raft.NodeID
	Strategy    RoutingStrategy

	// 约束条件
	MaxLatency     time.Duration
	MinReplicas    int
	ConsistencyReq ReadConsistencyLevel

	// 状态信息
	IsActive  bool
	CreatedAt time.Time
	LastUsed  time.Time
	UseCount  int64
}

// LoadBalancer 负载均衡器
type LoadBalancer struct {
	mu sync.RWMutex

	// 负载均衡状态
	method             LoadBalancingMethod
	roundRobinCounters map[raft.DataCenterID]int64
	weightMap          map[raft.NodeID]int
	connCounts         map[raft.NodeID]int64

	// 性能统计
	latencyMap map[raft.NodeID]time.Duration
	errorRates map[raft.NodeID]float64
}

// HealthChecker 健康检查器
type HealthChecker struct {
	mu sync.RWMutex

	// 健康状态
	nodeHealth map[raft.NodeID]*NodeHealthInfo
	dcHealth   map[raft.DataCenterID]*DCHealthInfo

	// 检查配置
	checkInterval time.Duration
	timeout       time.Duration
	retryCount    int
}

// RouterMetrics 路由器指标
type RouterMetrics struct {
	mu sync.RWMutex

	// 请求统计
	TotalRequests  int64
	ReadRequests   int64
	WriteRequests  int64
	RoutingLatency time.Duration

	// 成功率统计
	SuccessfulRoutes int64
	FailedRoutes     int64
	RetryCount       int64

	// DC统计
	DCRequestCounts map[raft.DataCenterID]int64
	DCLatencies     map[raft.DataCenterID]time.Duration
	DCSuccessRates  map[raft.DataCenterID]float64

	// 一致性统计
	StrongReads       int64
	EventualReads     int64
	LinearizableReads int64
	StaleReadCount    int64

	// 性能统计
	AverageLatency time.Duration
	P95Latency     time.Duration
	P99Latency     time.Duration
	ThroughputRPS  float64

	// 错误统计
	RoutingErrors     int64
	TimeoutErrors     int64
	ConsistencyErrors int64
}

// 相关类型定义
type RouteStats struct {
	RequestCount   int64
	ErrorCount     int64
	AverageLatency time.Duration
	LastUsed       time.Time
}

type NodeHealthInfo struct {
	IsHealthy    bool
	LastCheck    time.Time
	ResponseTime time.Duration
	ErrorCount   int64
	Availability float64
}

type DCHealthInfo struct {
	IsHealthy      bool
	HealthyNodes   int
	TotalNodes     int
	AverageLatency time.Duration
	LastUpdate     time.Time
}

// RoutingDecision 路由决策
type RoutingDecision struct {
	RequestType RequestType
	TargetNode  raft.NodeID
	TargetDC    raft.DataCenterID
	Route       *Route
	Latency     time.Duration
	Consistency ReadConsistencyLevel
	CreatedAt   time.Time
}

// NewReadWriteRouter 创建读写分离路由器
func NewReadWriteRouter(nodeID raft.NodeID, raftConfig *raft.Config) *ReadWriteRouter {
	config := DefaultReadWriteRouterConfig()
	ctx, cancel := context.WithCancel(context.Background())

	router := &ReadWriteRouter{
		nodeID:       nodeID,
		config:       config,
		raftConfig:   raftConfig,
		logger:       log.New(log.Writer(), fmt.Sprintf("[read-write-router-%s] ", nodeID), log.LstdFlags),
		dataCenters:  make(map[raft.DataCenterID]*DataCenterInfo),
		readReplicas: make(map[raft.DataCenterID][]raft.NodeID),
		writeTargets: make(map[raft.DataCenterID][]raft.NodeID),
		ctx:          ctx,
		cancel:       cancel,
		stopCh:       make(chan struct{}),
	}

	// 初始化组件
	router.initializeComponents()
	router.discoverDataCenters()

	return router
}

// initializeComponents 初始化组件
func (rwr *ReadWriteRouter) initializeComponents() {
	// 初始化路由表
	rwr.routingTable = &RoutingTable{
		readRoutes:  make(map[string]*Route),
		writeRoutes: make(map[string]*Route),
		routeStats:  make(map[string]*RouteStats),
	}

	// 初始化负载均衡器
	rwr.loadBalancer = &LoadBalancer{
		method:             rwr.config.LoadBalancingMethod,
		roundRobinCounters: make(map[raft.DataCenterID]int64),
		weightMap:          make(map[raft.NodeID]int),
		connCounts:         make(map[raft.NodeID]int64),
		latencyMap:         make(map[raft.NodeID]time.Duration),
		errorRates:         make(map[raft.NodeID]float64),
	}

	// 初始化健康检查器
	rwr.healthChecker = &HealthChecker{
		nodeHealth:    make(map[raft.NodeID]*NodeHealthInfo),
		dcHealth:      make(map[raft.DataCenterID]*DCHealthInfo),
		checkInterval: time.Duration(rwr.config.HealthCheckIntervalMs) * time.Millisecond,
		timeout:       time.Duration(rwr.config.RetryTimeoutMs) * time.Millisecond,
		retryCount:    rwr.config.RetryAttempts,
	}

	// 初始化指标收集器
	rwr.metrics = &RouterMetrics{
		DCRequestCounts: make(map[raft.DataCenterID]int64),
		DCLatencies:     make(map[raft.DataCenterID]time.Duration),
		DCSuccessRates:  make(map[raft.DataCenterID]float64),
	}
}

// discoverDataCenters 发现数据中心
func (rwr *ReadWriteRouter) discoverDataCenters() {
	if rwr.raftConfig.MultiDC == nil || !rwr.raftConfig.MultiDC.Enabled {
		rwr.logger.Printf("多DC未启用，跳过数据中心发现")
		return
	}

	rwr.localDC = rwr.raftConfig.MultiDC.LocalDataCenter.ID
	rwr.primaryDC = rwr.config.PrimaryDC
	if rwr.primaryDC == "" {
		rwr.primaryDC = rwr.localDC // 默认本地DC为主DC
	}

	// 按数据中心分组节点
	dcNodes := make(map[raft.DataCenterID][]raft.NodeID)
	for _, server := range rwr.raftConfig.Servers {
		dcNodes[server.DataCenter] = append(dcNodes[server.DataCenter], server.ID)
	}

	// 初始化数据中心信息
	for dcID, nodes := range dcNodes {
		isPrimary := dcID == rwr.primaryDC
		isLocal := dcID == rwr.localDC

		dcInfo := &DataCenterInfo{
			ID:               dcID,
			Nodes:            nodes,
			IsPrimary:        isPrimary,
			IsLocal:          isLocal,
			IsHealthy:        true,
			LastPing:         time.Now(),
			ConsistencyLevel: ConsistencyEventual,
		}

		// 设置网络信息
		if isLocal {
			dcInfo.Latency = 1 * time.Millisecond
		} else {
			dcInfo.Latency = 50 * time.Millisecond // 默认跨DC延迟
		}

		rwr.dataCenters[dcID] = dcInfo

		// 配置读写目标
		if isPrimary || rwr.config.EnableReadReplication {
			rwr.readReplicas[dcID] = nodes
		}
		if isPrimary {
			rwr.writeTargets[dcID] = nodes
		}

		// 初始化健康信息
		rwr.healthChecker.dcHealth[dcID] = &DCHealthInfo{
			IsHealthy:      true,
			HealthyNodes:   len(nodes),
			TotalNodes:     len(nodes),
			AverageLatency: dcInfo.Latency,
			LastUpdate:     time.Now(),
		}

		// 初始化节点健康信息
		for _, nodeID := range nodes {
			rwr.healthChecker.nodeHealth[nodeID] = &NodeHealthInfo{
				IsHealthy:    true,
				LastCheck:    time.Now(),
				ResponseTime: dcInfo.Latency,
				Availability: 1.0,
			}
		}

		rwr.logger.Printf("发现数据中心: DC=%s, 节点数=%d, 主DC=%t, 本地=%t",
			dcID, len(nodes), isPrimary, isLocal)
	}

	// 创建默认路由
	rwr.createDefaultRoutes()
}

// createDefaultRoutes 创建默认路由
func (rwr *ReadWriteRouter) createDefaultRoutes() {
	// 默认读路由：就近原则
	readRoute := &Route{
		ID:             "default-read",
		Type:           RequestTypeRead,
		Pattern:        "*",
		Priority:       100,
		Strategy:       rwr.config.ReadRoutingStrategy,
		MaxLatency:     time.Duration(rwr.config.MaxReadLatencyMs) * time.Millisecond,
		MinReplicas:    rwr.config.ReadReplicaCount,
		ConsistencyReq: rwr.config.ReadConsistency,
		IsActive:       true,
		CreatedAt:      time.Now(),
	}

	// 设置读路由目标
	if rwr.config.PreferLocalDC {
		readRoute.TargetDCs = []raft.DataCenterID{rwr.localDC}
		if nodes, exists := rwr.readReplicas[rwr.localDC]; exists {
			readRoute.TargetNodes = nodes
		}
	} else {
		for dcID := range rwr.readReplicas {
			readRoute.TargetDCs = append(readRoute.TargetDCs, dcID)
		}
	}

	rwr.routingTable.defaultReadRoute = readRoute
	rwr.routingTable.readRoutes["default-read"] = readRoute

	// 默认写路由：主DC
	writeRoute := &Route{
		ID:             "default-write",
		Type:           RequestTypeWrite,
		Pattern:        "*",
		Priority:       100,
		Strategy:       RoutingPrimaryDC,
		TargetDCs:      []raft.DataCenterID{rwr.primaryDC},
		MaxLatency:     time.Duration(rwr.config.RetryTimeoutMs) * time.Millisecond,
		ConsistencyReq: ReadConsistencyStrong,
		IsActive:       true,
		CreatedAt:      time.Now(),
	}

	if nodes, exists := rwr.writeTargets[rwr.primaryDC]; exists {
		writeRoute.TargetNodes = nodes
	}

	rwr.routingTable.defaultWriteRoute = writeRoute
	rwr.routingTable.writeRoutes["default-write"] = writeRoute

	rwr.logger.Printf("创建默认路由 - 读路由目标DC数=%d, 写路由目标DC=%s",
		len(readRoute.TargetDCs), rwr.primaryDC)
}

// Start 启动读写分离路由器
func (rwr *ReadWriteRouter) Start() error {
	rwr.mu.Lock()
	defer rwr.mu.Unlock()

	if rwr.running {
		return fmt.Errorf("读写分离路由器已在运行")
	}

	rwr.logger.Printf("启动读写分离路由器")

	// 启动工作线程
	rwr.wg.Add(2)
	go rwr.healthCheckLoop()
	go rwr.metricsCollectionLoop()

	rwr.running = true
	rwr.logger.Printf("读写分离路由器启动成功")

	return nil
}

// Stop 停止读写分离路由器
func (rwr *ReadWriteRouter) Stop() error {
	rwr.mu.Lock()
	defer rwr.mu.Unlock()

	if !rwr.running {
		return nil
	}

	rwr.logger.Printf("停止读写分离路由器")

	// 发送停止信号
	close(rwr.stopCh)
	rwr.cancel()

	// 等待工作线程结束
	rwr.wg.Wait()

	rwr.running = false
	rwr.logger.Printf("读写分离路由器已停止")

	return nil
}

// RouteRequest 路由请求
func (rwr *ReadWriteRouter) RouteRequest(requestType RequestType, key string, consistency ReadConsistencyLevel) (*RoutingDecision, error) {
	start := time.Now()
	defer func() {
		rwr.updateRoutingMetrics(requestType, time.Since(start))
	}()

	rwr.mu.RLock()
	defer rwr.mu.RUnlock()

	// 选择路由策略
	var route *Route
	if requestType == RequestTypeRead {
		route = rwr.selectReadRoute(key, consistency)
	} else {
		route = rwr.selectWriteRoute(key)
	}

	if route == nil {
		return nil, fmt.Errorf("无法找到合适的路由")
	}

	// 负载均衡选择节点
	targetNode, targetDC, err := rwr.selectTargetNode(route)
	if err != nil {
		return nil, fmt.Errorf("节点选择失败: %v", err)
	}

	// 创建路由决策
	decision := &RoutingDecision{
		RequestType: requestType,
		TargetNode:  targetNode,
		TargetDC:    targetDC,
		Route:       route,
		Latency:     rwr.getExpectedLatency(targetDC),
		Consistency: consistency,
		CreatedAt:   time.Now(),
	}

	rwr.logger.Printf("路由决策: 类型=%d, 目标节点=%s, 目标DC=%s, 延迟=%v",
		requestType, targetNode, targetDC, decision.Latency)

	return decision, nil
}

// 内部方法实现
func (rwr *ReadWriteRouter) selectReadRoute(key string, consistency ReadConsistencyLevel) *Route {
	// 简化实现：返回默认读路由
	route := rwr.routingTable.defaultReadRoute

	// 根据一致性要求调整路由
	if consistency == ReadConsistencyStrong || consistency == ReadConsistencyLinearizable {
		// 强一致性读必须路由到主DC
		route.TargetDCs = []raft.DataCenterID{rwr.primaryDC}
		if nodes, exists := rwr.writeTargets[rwr.primaryDC]; exists {
			route.TargetNodes = nodes
		}
	}

	route.LastUsed = time.Now()
	route.UseCount++

	return route
}

func (rwr *ReadWriteRouter) selectWriteRoute(key string) *Route {
	// 写请求总是路由到主DC
	route := rwr.routingTable.defaultWriteRoute
	route.LastUsed = time.Now()
	route.UseCount++
	return route
}

func (rwr *ReadWriteRouter) selectTargetNode(route *Route) (raft.NodeID, raft.DataCenterID, error) {
	if len(route.TargetDCs) == 0 {
		return "", "", fmt.Errorf("路由没有目标DC")
	}

	// 选择最佳DC
	bestDC := rwr.selectBestDC(route.TargetDCs, route.Strategy)

	// 在选定DC中负载均衡选择节点
	targetNode, err := rwr.loadBalanceNode(bestDC, route.Type)
	if err != nil {
		return "", "", err
	}

	return targetNode, bestDC, nil
}

func (rwr *ReadWriteRouter) selectBestDC(targetDCs []raft.DataCenterID, strategy RoutingStrategy) raft.DataCenterID {
	switch strategy {
	case RoutingNearestDC, RoutingLocalFirst:
		// 优先本地DC
		for _, dcID := range targetDCs {
			if dcID == rwr.localDC {
				if dcInfo, exists := rwr.dataCenters[dcID]; exists && dcInfo.IsHealthy {
					return dcID
				}
			}
		}
		// 如果本地DC不可用，选择延迟最低的
		return rwr.selectLowestLatencyDC(targetDCs)

	case RoutingLeastLatency:
		return rwr.selectLowestLatencyDC(targetDCs)

	case RoutingPrimaryDC:
		return rwr.primaryDC

	default:
		// 默认返回第一个健康的DC
		for _, dcID := range targetDCs {
			if dcInfo, exists := rwr.dataCenters[dcID]; exists && dcInfo.IsHealthy {
				return dcID
			}
		}
		return targetDCs[0]
	}
}

func (rwr *ReadWriteRouter) selectLowestLatencyDC(targetDCs []raft.DataCenterID) raft.DataCenterID {
	bestDC := targetDCs[0]
	bestLatency := time.Hour

	for _, dcID := range targetDCs {
		if dcInfo, exists := rwr.dataCenters[dcID]; exists && dcInfo.IsHealthy {
			if dcInfo.Latency < bestLatency {
				bestLatency = dcInfo.Latency
				bestDC = dcID
			}
		}
	}

	return bestDC
}

func (rwr *ReadWriteRouter) loadBalanceNode(dcID raft.DataCenterID, requestType RequestType) (raft.NodeID, error) {
	var nodes []raft.NodeID

	if requestType == RequestTypeRead {
		if readNodes, exists := rwr.readReplicas[dcID]; exists {
			nodes = readNodes
		}
	} else {
		if writeNodes, exists := rwr.writeTargets[dcID]; exists {
			nodes = writeNodes
		}
	}

	if len(nodes) == 0 {
		return "", fmt.Errorf("DC %s 没有可用节点", dcID)
	}

	// 过滤健康节点
	healthyNodes := make([]raft.NodeID, 0)
	for _, nodeID := range nodes {
		if healthInfo, exists := rwr.healthChecker.nodeHealth[nodeID]; exists && healthInfo.IsHealthy {
			healthyNodes = append(healthyNodes, nodeID)
		}
	}

	if len(healthyNodes) == 0 {
		return "", fmt.Errorf("DC %s 没有健康节点", dcID)
	}

	// 负载均衡选择
	switch rwr.loadBalancer.method {
	case LoadBalanceRoundRobin:
		rwr.loadBalancer.mu.Lock()
		counter := rwr.loadBalancer.roundRobinCounters[dcID]
		selectedNode := healthyNodes[counter%int64(len(healthyNodes))]
		rwr.loadBalancer.roundRobinCounters[dcID] = counter + 1
		rwr.loadBalancer.mu.Unlock()
		return selectedNode, nil

	case LoadBalanceLeastLatency:
		return rwr.selectLowestLatencyNode(healthyNodes), nil

	default:
		// 默认随机选择
		return healthyNodes[0], nil
	}
}

func (rwr *ReadWriteRouter) selectLowestLatencyNode(nodes []raft.NodeID) raft.NodeID {
	bestNode := nodes[0]
	bestLatency := time.Hour

	rwr.loadBalancer.mu.RLock()
	defer rwr.loadBalancer.mu.RUnlock()

	for _, nodeID := range nodes {
		if latency, exists := rwr.loadBalancer.latencyMap[nodeID]; exists {
			if latency < bestLatency {
				bestLatency = latency
				bestNode = nodeID
			}
		}
	}

	return bestNode
}

func (rwr *ReadWriteRouter) getExpectedLatency(dcID raft.DataCenterID) time.Duration {
	if dcInfo, exists := rwr.dataCenters[dcID]; exists {
		return dcInfo.Latency
	}
	return 50 * time.Millisecond // 默认延迟
}

func (rwr *ReadWriteRouter) updateRoutingMetrics(requestType RequestType, latency time.Duration) {
	rwr.metrics.mu.Lock()
	defer rwr.metrics.mu.Unlock()

	rwr.metrics.TotalRequests++
	if requestType == RequestTypeRead {
		rwr.metrics.ReadRequests++
	} else {
		rwr.metrics.WriteRequests++
	}

	// 更新延迟统计
	rwr.metrics.RoutingLatency = (rwr.metrics.RoutingLatency + latency) / 2
	if latency > rwr.metrics.P99Latency {
		rwr.metrics.P99Latency = latency
	}
}

// 工作线程循环
func (rwr *ReadWriteRouter) healthCheckLoop() {
	defer rwr.wg.Done()
	rwr.logger.Printf("健康检查循环已启动")

	ticker := time.NewTicker(rwr.healthChecker.checkInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			rwr.performHealthChecks()
		case <-rwr.stopCh:
			rwr.logger.Printf("健康检查循环已停止")
			return
		}
	}
}

func (rwr *ReadWriteRouter) metricsCollectionLoop() {
	defer rwr.wg.Done()
	rwr.logger.Printf("指标收集循环已启动")

	ticker := time.NewTicker(time.Duration(rwr.config.MetricsIntervalMs) * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			rwr.updateMetrics()
		case <-rwr.stopCh:
			rwr.logger.Printf("指标收集循环已停止")
			return
		}
	}
}

func (rwr *ReadWriteRouter) performHealthChecks() {
	for dcID, dcInfo := range rwr.dataCenters {
		healthyCount := 0

		for _, nodeID := range dcInfo.Nodes {
			// 简单的健康检查逻辑
			nodeHealth := rwr.healthChecker.nodeHealth[nodeID]
			nodeHealth.LastCheck = time.Now()

			// 模拟健康检查结果（实际应该是网络检查）
			nodeHealth.IsHealthy = true
			nodeHealth.ResponseTime = dcInfo.Latency
			nodeHealth.Availability = 0.99

			if nodeHealth.IsHealthy {
				healthyCount++
			}
		}

		// 更新DC健康状态
		dcHealth := rwr.healthChecker.dcHealth[dcID]
		dcHealth.HealthyNodes = healthyCount
		dcHealth.IsHealthy = healthyCount > 0
		dcHealth.LastUpdate = time.Now()

		dcInfo.IsHealthy = dcHealth.IsHealthy

		rwr.logger.Printf("健康检查: DC=%s, 健康节点=%d/%d",
			dcID, healthyCount, len(dcInfo.Nodes))
	}
}

func (rwr *ReadWriteRouter) updateMetrics() {
	rwr.metrics.mu.Lock()
	defer rwr.metrics.mu.Unlock()

	// 计算吞吐量
	rwr.metrics.ThroughputRPS = float64(rwr.metrics.TotalRequests) /
		time.Since(time.Now().Add(-time.Duration(rwr.config.MetricsIntervalMs)*time.Millisecond)).Seconds()

	// 更新DC统计
	for dcID := range rwr.dataCenters {
		if count, exists := rwr.metrics.DCRequestCounts[dcID]; exists {
			rwr.logger.Printf("DC指标: %s, 请求数=%d", dcID, count)
		}
	}
}

// GetMetrics 获取路由器指标
func (rwr *ReadWriteRouter) GetMetrics() *RouterMetrics {
	rwr.metrics.mu.RLock()
	defer rwr.metrics.mu.RUnlock()

	// 创建指标副本
	metricsCopy := &RouterMetrics{}
	*metricsCopy = *rwr.metrics
	metricsCopy.DCRequestCounts = make(map[raft.DataCenterID]int64)
	metricsCopy.DCLatencies = make(map[raft.DataCenterID]time.Duration)
	metricsCopy.DCSuccessRates = make(map[raft.DataCenterID]float64)

	for dcID, count := range rwr.metrics.DCRequestCounts {
		metricsCopy.DCRequestCounts[dcID] = count
	}
	for dcID, latency := range rwr.metrics.DCLatencies {
		metricsCopy.DCLatencies[dcID] = latency
	}
	for dcID, rate := range rwr.metrics.DCSuccessRates {
		metricsCopy.DCSuccessRates[dcID] = rate
	}

	return metricsCopy
}

// GetDataCenterInfo 获取数据中心信息
func (rwr *ReadWriteRouter) GetDataCenterInfo() map[raft.DataCenterID]*DataCenterInfo {
	rwr.mu.RLock()
	defer rwr.mu.RUnlock()

	dcInfoCopy := make(map[raft.DataCenterID]*DataCenterInfo)
	for dcID, dcInfo := range rwr.dataCenters {
		infoCopy := &DataCenterInfo{}
		*infoCopy = *dcInfo
		dcInfoCopy[dcID] = infoCopy
	}

	return dcInfoCopy
}
