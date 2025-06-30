/*
* @Author: Lzww0608
* @Date: 2025-6-30 22:21:57
* @LastEditors: Lzww0608
* @LastEditTime: 2025-06-30 22:21:57
* @Description: ConcordKV中央拓扑服务 - 智能客户端拓扑缓存
 */
package topology

import (
	"bufio"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"

	"raftserver/raft"
)

// TopologyClient 智能客户端拓扑缓存
// 复用现有HTTP客户端框架，添加拓扑感知能力
type TopologyClient struct {
	// 复用现有HTTP客户端框架
	httpClient *http.Client

	// 拓扑缓存
	localCache    *TopologySnapshot
	cachePolicy   *CachePolicy
	updateChannel chan *TopologyEvent

	// 服务器连接
	serverURLs    []string
	currentServer int

	// 同步控制
	mu         sync.RWMutex
	shutdownCh chan struct{}
	logger     *log.Logger

	// 配置
	config *ClientConfig
}

// CachePolicy 缓存策略配置
type CachePolicy struct {
	TTL              time.Duration `yaml:"ttl"`              // 缓存生存时间
	RefreshThreshold time.Duration `yaml:"refreshThreshold"` // 刷新阈值
	MaxEntries       int           `yaml:"maxEntries"`       // 最大缓存条目
	CompressionLevel int           `yaml:"compressionLevel"` // 压缩级别
}

// ClientConfig 客户端配置
type ClientConfig struct {
	ServerURLs        []string      `yaml:"serverUrls"`        // 拓扑服务器地址列表
	RequestTimeout    time.Duration `yaml:"requestTimeout"`    // 请求超时
	RetryInterval     time.Duration `yaml:"retryInterval"`     // 重试间隔
	MaxRetries        int           `yaml:"maxRetries"`        // 最大重试次数
	HealthCheckPeriod time.Duration `yaml:"healthCheckPeriod"` // 健康检查周期
	CachePolicy       *CachePolicy  `yaml:"cachePolicy"`       // 缓存策略
}

// ShardRoute 分片路由信息
type ShardRoute struct {
	ShardID      string        `json:"shardId"`
	KeyRange     *KeyRange     `json:"keyRange"`
	PrimaryNode  raft.NodeID   `json:"primaryNode"`
	ReplicaNodes []raft.NodeID `json:"replicaNodes"`
	HealthyNodes []raft.NodeID `json:"healthyNodes"`
	LastUpdate   time.Time     `json:"lastUpdate"`
}

// NodeInfo 节点信息
type NodeInfo struct {
	NodeID    raft.NodeID  `json:"nodeId"`
	Address   string       `json:"address"`
	Health    HealthStatus `json:"health"`
	Load      LoadInfo     `json:"load"`
	Available bool         `json:"available"`
	LastCheck time.Time    `json:"lastCheck"`
}

// NewTopologyClient 创建新的拓扑客户端
func NewTopologyClient(config *ClientConfig) *TopologyClient {
	return &TopologyClient{
		// 复用现有HTTP客户端
		httpClient: &http.Client{
			Timeout: config.RequestTimeout,
		},

		// 初始化拓扑缓存
		localCache:    nil,
		cachePolicy:   config.CachePolicy,
		updateChannel: make(chan *TopologyEvent, 100),

		// 服务器连接配置
		serverURLs:    config.ServerURLs,
		currentServer: 0,

		// 控制通道
		shutdownCh: make(chan struct{}),

		// 配置和日志
		config: config,
		logger: log.New(log.Writer(), "[topology-client] ", log.LstdFlags),
	}
}

// Start 启动拓扑客户端
func (tc *TopologyClient) Start() error {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	tc.logger.Printf("启动拓扑客户端...")

	// 初始化拓扑缓存
	if err := tc.initializeCache(); err != nil {
		return fmt.Errorf("初始化拓扑缓存失败: %w", err)
	}

	// 启动SSE事件监听
	go tc.startSSEListener()

	// 启动缓存刷新
	go tc.cacheRefreshLoop()

	// 启动服务器健康检查
	go tc.serverHealthCheckLoop()

	tc.logger.Printf("拓扑客户端启动成功")
	return nil
}

// Stop 停止拓扑客户端
func (tc *TopologyClient) Stop() error {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	tc.logger.Printf("停止拓扑客户端...")

	// 发送关闭信号
	close(tc.shutdownCh)

	tc.logger.Printf("拓扑客户端已停止")
	return nil
}

// GetShardRoute 获取键的分片路由信息
func (tc *TopologyClient) GetShardRoute(key string) (*ShardRoute, error) {
	tc.mu.RLock()
	defer tc.mu.RUnlock()

	if tc.localCache == nil {
		return nil, fmt.Errorf("拓扑缓存未初始化")
	}

	// 根据键查找对应的分片
	for _, shardInfo := range tc.localCache.ShardMap {
		if tc.isKeyInRange(key, shardInfo.KeyRange) {
			// 过滤健康的节点
			healthyNodes := tc.filterHealthyNodes(append([]raft.NodeID{shardInfo.PrimaryNode}, shardInfo.ReplicaNodes...))

			return &ShardRoute{
				ShardID:      shardInfo.ShardID,
				KeyRange:     shardInfo.KeyRange,
				PrimaryNode:  shardInfo.PrimaryNode,
				ReplicaNodes: shardInfo.ReplicaNodes,
				HealthyNodes: healthyNodes,
				LastUpdate:   shardInfo.LastUpdate,
			}, nil
		}
	}

	return nil, fmt.Errorf("未找到键 %s 对应的分片", key)
}

// GetPrimaryNode 获取键对应的主节点
func (tc *TopologyClient) GetPrimaryNode(key string) (raft.NodeID, error) {
	route, err := tc.GetShardRoute(key)
	if err != nil {
		return "", err
	}

	// 检查主节点是否健康
	if tc.isNodeHealthy(route.PrimaryNode) {
		return route.PrimaryNode, nil
	}

	// 如果主节点不健康，返回错误
	return "", fmt.Errorf("主节点 %s 不健康", route.PrimaryNode)
}

// GetReadNodes 获取键对应的可读节点列表（按优先级排序）
func (tc *TopologyClient) GetReadNodes(key string) ([]raft.NodeID, error) {
	route, err := tc.GetShardRoute(key)
	if err != nil {
		return nil, err
	}

	// 优先级：主节点 > 副本节点
	readNodes := make([]raft.NodeID, 0)

	// 如果主节点健康，优先使用主节点
	if tc.isNodeHealthy(route.PrimaryNode) {
		readNodes = append(readNodes, route.PrimaryNode)
	}

	// 添加健康的副本节点
	for _, replica := range route.ReplicaNodes {
		if tc.isNodeHealthy(replica) {
			readNodes = append(readNodes, replica)
		}
	}

	if len(readNodes) == 0 {
		return nil, fmt.Errorf("没有可用的读节点")
	}

	return readNodes, nil
}

// GetNodeInfo 获取节点信息
func (tc *TopologyClient) GetNodeInfo(nodeID raft.NodeID) (*NodeInfo, error) {
	tc.mu.RLock()
	defer tc.mu.RUnlock()

	if tc.localCache == nil {
		return nil, fmt.Errorf("拓扑缓存未初始化")
	}

	nodeStatus, exists := tc.localCache.NodeStatus[nodeID]
	if !exists {
		return nil, fmt.Errorf("节点 %s 不存在", nodeID)
	}

	return &NodeInfo{
		NodeID:    nodeStatus.NodeID,
		Address:   nodeStatus.Address,
		Health:    nodeStatus.Health,
		Load:      nodeStatus.Load,
		Available: nodeStatus.Health == HealthStatusHealthy,
		LastCheck: nodeStatus.LastSeen,
	}, nil
}

// GetTopologyVersion 获取当前拓扑版本
func (tc *TopologyClient) GetTopologyVersion() int64 {
	tc.mu.RLock()
	defer tc.mu.RUnlock()

	if tc.localCache == nil {
		return 0
	}
	return tc.localCache.Version
}

// RefreshCache 手动刷新缓存
func (tc *TopologyClient) RefreshCache() error {
	tc.logger.Printf("手动刷新拓扑缓存...")

	snapshot, err := tc.fetchTopologySnapshot()
	if err != nil {
		return fmt.Errorf("获取拓扑快照失败: %w", err)
	}

	tc.mu.Lock()
	tc.localCache = snapshot
	tc.mu.Unlock()

	tc.logger.Printf("拓扑缓存刷新成功，版本: %d", snapshot.Version)
	return nil
}

// 私有方法实现

// initializeCache 初始化拓扑缓存
func (tc *TopologyClient) initializeCache() error {
	tc.logger.Printf("初始化拓扑缓存...")

	// 获取初始拓扑快照
	snapshot, err := tc.fetchTopologySnapshot()
	if err != nil {
		return fmt.Errorf("获取初始拓扑快照失败: %w", err)
	}

	tc.localCache = snapshot
	tc.logger.Printf("拓扑缓存初始化成功，版本: %d", snapshot.Version)
	return nil
}

// fetchTopologySnapshot 从服务器获取拓扑快照
func (tc *TopologyClient) fetchTopologySnapshot() (*TopologySnapshot, error) {
	for attempt := 0; attempt < tc.config.MaxRetries; attempt++ {
		serverURL := tc.getCurrentServerURL()
		url := fmt.Sprintf("%s/api/topology/snapshot", serverURL)

		resp, err := tc.httpClient.Get(url)
		if err != nil {
			tc.logger.Printf("请求服务器失败 (尝试 %d/%d): %v", attempt+1, tc.config.MaxRetries, err)
			tc.switchToNextServer()
			time.Sleep(tc.config.RetryInterval)
			continue
		}
		defer resp.Body.Close()

		if resp.StatusCode != http.StatusOK {
			tc.logger.Printf("服务器响应错误 (状态码: %d)", resp.StatusCode)
			tc.switchToNextServer()
			time.Sleep(tc.config.RetryInterval)
			continue
		}

		var snapshot TopologySnapshot
		if err := json.NewDecoder(resp.Body).Decode(&snapshot); err != nil {
			tc.logger.Printf("解析响应失败: %v", err)
			continue
		}

		return &snapshot, nil
	}

	return nil, fmt.Errorf("所有服务器都不可用")
}

// startSSEListener 启动SSE事件监听
func (tc *TopologyClient) startSSEListener() {
	for {
		select {
		case <-tc.shutdownCh:
			return
		default:
			tc.connectSSE()
		}
	}
}

// connectSSE 连接SSE事件流
func (tc *TopologyClient) connectSSE() {
	serverURL := tc.getCurrentServerURL()
	url := fmt.Sprintf("%s/api/topology/subscribe", serverURL)

	tc.logger.Printf("连接SSE事件流: %s", url)

	resp, err := tc.httpClient.Get(url)
	if err != nil {
		tc.logger.Printf("SSE连接失败: %v", err)
		time.Sleep(tc.config.RetryInterval)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		tc.logger.Printf("SSE连接响应错误 (状态码: %d)", resp.StatusCode)
		time.Sleep(tc.config.RetryInterval)
		return
	}

	tc.logger.Printf("SSE连接已建立")

	// 读取SSE事件流
	scanner := bufio.NewScanner(resp.Body)
	for scanner.Scan() {
		select {
		case <-tc.shutdownCh:
			return
		default:
			line := scanner.Text()
			if line == "" {
				continue
			}

			// 解析SSE事件
			if err := tc.parseSSEEvent(line); err != nil {
				tc.logger.Printf("解析SSE事件失败: %v", err)
			}
		}
	}

	if err := scanner.Err(); err != nil {
		tc.logger.Printf("SSE连接中断: %v", err)
	}

	tc.logger.Printf("SSE连接已断开，准备重连...")
	time.Sleep(tc.config.RetryInterval)
}

// parseSSEEvent 解析SSE事件
func (tc *TopologyClient) parseSSEEvent(line string) error {
	// 简化SSE解析，实际应该更严格
	if len(line) > 6 && line[:6] == "data: " {
		eventData := line[6:]

		var event TopologyEvent
		if err := json.Unmarshal([]byte(eventData), &event); err != nil {
			return fmt.Errorf("解析事件数据失败: %w", err)
		}

		// 处理事件
		tc.handleTopologyEvent(&event)
	}

	return nil
}

// handleTopologyEvent 处理拓扑事件
func (tc *TopologyClient) handleTopologyEvent(event *TopologyEvent) {
	tc.logger.Printf("收到拓扑事件: 类型=%s, 版本=%d", event.Type.String(), event.Version)

	// 根据事件类型更新本地缓存
	switch event.Type {
	case EventNodeHealthChange:
		tc.updateNodeHealth(event)
	case EventShardMove:
		tc.updateShardMapping(event)
	case EventConfigChange:
		// 配置变更，刷新整个缓存
		go tc.RefreshCache()
	default:
		// 其他事件类型，也刷新缓存
		go tc.RefreshCache()
	}
}

// updateNodeHealth 更新节点健康状态
func (tc *TopologyClient) updateNodeHealth(event *TopologyEvent) {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	if tc.localCache == nil {
		return
	}

	if nodeStatus, exists := tc.localCache.NodeStatus[event.NodeID]; exists {
		if newHealth, ok := event.NewValue.(HealthStatus); ok {
			nodeStatus.Health = newHealth
			nodeStatus.LastSeen = event.Timestamp
			tc.logger.Printf("更新节点健康状态: %s -> %s", event.NodeID, newHealth.String())
		}
	}
}

// updateShardMapping 更新分片映射
func (tc *TopologyClient) updateShardMapping(event *TopologyEvent) {
	// 分片映射变更比较复杂，这里简化处理，直接刷新缓存
	go tc.RefreshCache()
}

// cacheRefreshLoop 缓存刷新循环
func (tc *TopologyClient) cacheRefreshLoop() {
	ticker := time.NewTicker(tc.cachePolicy.RefreshThreshold)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			if tc.shouldRefreshCache() {
				tc.RefreshCache()
			}
		case <-tc.shutdownCh:
			return
		}
	}
}

// shouldRefreshCache 检查是否需要刷新缓存
func (tc *TopologyClient) shouldRefreshCache() bool {
	tc.mu.RLock()
	defer tc.mu.RUnlock()

	if tc.localCache == nil {
		return true
	}

	// 检查缓存是否过期
	return time.Since(tc.localCache.Timestamp) > tc.cachePolicy.TTL
}

// serverHealthCheckLoop 服务器健康检查循环
func (tc *TopologyClient) serverHealthCheckLoop() {
	ticker := time.NewTicker(tc.config.HealthCheckPeriod)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			tc.checkServerHealth()
		case <-tc.shutdownCh:
			return
		}
	}
}

// checkServerHealth 检查服务器健康状态
func (tc *TopologyClient) checkServerHealth() {
	serverURL := tc.getCurrentServerURL()
	url := fmt.Sprintf("%s/health", serverURL)

	resp, err := tc.httpClient.Get(url)
	if err != nil {
		tc.logger.Printf("服务器健康检查失败: %v", err)
		tc.switchToNextServer()
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		tc.logger.Printf("服务器健康检查响应错误 (状态码: %d)", resp.StatusCode)
		tc.switchToNextServer()
	}
}

// isKeyInRange 检查键是否在范围内
func (tc *TopologyClient) isKeyInRange(key string, keyRange *KeyRange) bool {
	// 简化实现，实际应该根据具体的键分布算法
	return key >= keyRange.Start && key < keyRange.End
}

// filterHealthyNodes 过滤健康的节点
func (tc *TopologyClient) filterHealthyNodes(nodes []raft.NodeID) []raft.NodeID {
	healthyNodes := make([]raft.NodeID, 0)
	for _, nodeID := range nodes {
		if tc.isNodeHealthy(nodeID) {
			healthyNodes = append(healthyNodes, nodeID)
		}
	}
	return healthyNodes
}

// isNodeHealthy 检查节点是否健康
func (tc *TopologyClient) isNodeHealthy(nodeID raft.NodeID) bool {
	if tc.localCache == nil {
		return false
	}

	nodeStatus, exists := tc.localCache.NodeStatus[nodeID]
	if !exists {
		return false
	}

	return nodeStatus.Health == HealthStatusHealthy
}

// getCurrentServerURL 获取当前服务器URL
func (tc *TopologyClient) getCurrentServerURL() string {
	if len(tc.serverURLs) == 0 {
		return "http://localhost:8081" // 默认服务器
	}
	return tc.serverURLs[tc.currentServer]
}

// switchToNextServer 切换到下一个服务器
func (tc *TopologyClient) switchToNextServer() {
	if len(tc.serverURLs) > 1 {
		tc.currentServer = (tc.currentServer + 1) % len(tc.serverURLs)
		tc.logger.Printf("切换到服务器: %s", tc.getCurrentServerURL())
	}
}
