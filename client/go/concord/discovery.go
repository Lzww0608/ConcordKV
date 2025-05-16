package concord

import (
	"encoding/json"
	"errors"
	"fmt"
	"math/rand"
	"net/http"
	"sync"
	"time"
)

// 节点发现类型
const (
	DiscoveryTypeStatic  = "static"  // 静态列表
	DiscoveryTypeDNS     = "dns"     // DNS发现
	DiscoveryTypeService = "service" // 服务注册发现
)

// 负载均衡策略
const (
	BalanceRandom     = "random"     // 随机选择
	BalanceRoundRobin = "roundrobin" // 轮询
	BalanceWeighted   = "weighted"   // 加权（根据性能）
	BalanceLeastConn  = "leastconn"  // 最少连接
	BalanceConsistent = "consistent" // 一致性哈希
)

// DiscoveryConfig 节点发现配置
type DiscoveryConfig struct {
	// 发现类型
	Type string
	// 服务注册地址（用于服务发现类型）
	ServiceRegistryURL string
	// 服务名称（用于服务发现和DNS类型）
	ServiceName string
	// DNS域名（用于DNS类型）
	DNSName string
	// 刷新间隔
	RefreshInterval time.Duration
	// 负载均衡策略
	BalanceStrategy string
	// 是否自动发现新节点
	AutoDiscover bool
}

// Discovery 节点发现服务
type Discovery struct {
	config         DiscoveryConfig
	client         *Client
	mu             sync.RWMutex
	knownEndpoints map[string]bool
	stopCh         chan struct{}
	isRunning      bool
	lastRefresh    time.Time
	currentIndex   int        // 用于轮询
	rrMutex        sync.Mutex // 用于保护轮询索引
}

// 创建节点发现服务
func newDiscovery(client *Client, config DiscoveryConfig) *Discovery {
	if config.RefreshInterval == 0 {
		config.RefreshInterval = 60 * time.Second
	}

	if config.BalanceStrategy == "" {
		config.BalanceStrategy = BalanceRoundRobin
	}

	d := &Discovery{
		config:         config,
		client:         client,
		knownEndpoints: make(map[string]bool),
		stopCh:         make(chan struct{}),
		currentIndex:   0,
	}

	// 初始化已知端点
	for _, endpoint := range client.config.Endpoints {
		d.knownEndpoints[endpoint] = true
	}

	// 初始化随机数生成器
	rand.Seed(time.Now().UnixNano())

	return d
}

// 启动节点发现
func (d *Discovery) Start() error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if d.isRunning {
		return nil
	}

	// 立即执行一次发现
	if err := d.discoverNodes(); err != nil {
		return err
	}

	d.isRunning = true
	go d.discoveryLoop()
	return nil
}

// 停止节点发现
func (d *Discovery) Stop() {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.isRunning {
		return
	}

	close(d.stopCh)
	d.isRunning = false
}

// 发现节点循环
func (d *Discovery) discoveryLoop() {
	ticker := time.NewTicker(d.config.RefreshInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			_ = d.discoverNodes()
		case <-d.stopCh:
			return
		}
	}
}

// 发现新节点
func (d *Discovery) discoverNodes() error {
	d.mu.Lock()
	defer d.mu.Unlock()

	var newEndpoints []string
	var err error

	switch d.config.Type {
	case DiscoveryTypeStatic:
		// 静态模式下不自动发现新节点
		d.lastRefresh = time.Now()
		return nil
	case DiscoveryTypeDNS:
		newEndpoints, err = d.discoverNodesFromDNS()
	case DiscoveryTypeService:
		newEndpoints, err = d.discoverNodesFromService()
	default:
		return errors.New("不支持的发现类型")
	}

	if err != nil {
		return err
	}

	// 更新节点列表
	added := d.updateEndpoints(newEndpoints)
	if len(added) > 0 {
		// 如果发现了新节点，初始化连接
		if err := d.client.initNewConnections(added); err != nil {
			return err
		}
	}

	d.lastRefresh = time.Now()
	return nil
}

// 从DNS发现节点
func (d *Discovery) discoverNodesFromDNS() ([]string, error) {
	// 在实际实现中，这里应该从DNS SRV记录解析服务地址
	// 这里使用简化版实现，实际项目中应使用net包中的DNS解析功能
	// 示例代码：
	/*
		_, addrs, err := net.LookupSRV("", "", d.config.DNSName)
		if err != nil {
			return nil, err
		}

		var endpoints []string
		for _, addr := range addrs {
			endpoint := fmt.Sprintf("%s:%d", addr.Target, addr.Port)
			endpoints = append(endpoints, endpoint)
		}

		return endpoints, nil
	*/

	// 简化版仅返回已知节点
	return nil, errors.New("DNS发现功能需要完整实现")
}

// 从服务注册中心发现节点
func (d *Discovery) discoverNodesFromService() ([]string, error) {
	if d.config.ServiceRegistryURL == "" {
		return nil, errors.New("服务注册地址未设置")
	}

	// 构建请求URL
	url := fmt.Sprintf("%s/services/%s", d.config.ServiceRegistryURL, d.config.ServiceName)

	// 发送HTTP请求到服务注册中心
	resp, err := http.Get(url)
	if err != nil {
		return nil, fmt.Errorf("访问服务注册中心失败: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("服务注册中心返回错误状态码: %d", resp.StatusCode)
	}

	// 解析响应
	var result struct {
		Endpoints []string `json:"endpoints"`
	}

	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, fmt.Errorf("解析服务注册中心响应失败: %w", err)
	}

	return result.Endpoints, nil
}

// 更新节点列表，返回新添加的节点
func (d *Discovery) updateEndpoints(endpoints []string) []string {
	var added []string

	// 添加新发现的节点
	for _, endpoint := range endpoints {
		if _, exists := d.knownEndpoints[endpoint]; !exists {
			d.knownEndpoints[endpoint] = true
			added = append(added, endpoint)
		}
	}

	return added
}

// GetEndpoint 根据负载均衡策略获取一个端点
func (d *Discovery) GetEndpoint() (string, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if len(d.knownEndpoints) == 0 {
		return "", ErrNoEndpoints
	}

	// 将已知端点转换为切片
	var endpoints []string
	for endpoint := range d.knownEndpoints {
		conn, ok := d.client.conns[endpoint]
		// 只选择健康的连接
		if ok && conn.isHealthy() {
			endpoints = append(endpoints, endpoint)
		}
	}

	if len(endpoints) == 0 {
		return "", ErrNoEndpoints
	}

	// 根据负载均衡策略选择节点
	switch d.config.BalanceStrategy {
	case BalanceRandom:
		return d.randomEndpoint(endpoints)
	case BalanceRoundRobin:
		return d.roundRobinEndpoint(endpoints)
	case BalanceWeighted:
		return d.weightedEndpoint(endpoints)
	case BalanceLeastConn:
		return d.leastConnEndpoint(endpoints)
	case BalanceConsistent:
		return d.consistentHashEndpoint(endpoints, "")
	default:
		// 默认使用轮询
		return d.roundRobinEndpoint(endpoints)
	}
}

// GetKeyEndpoint 根据键获取一个端点（用于一致性哈希）
func (d *Discovery) GetKeyEndpoint(key string) (string, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if len(d.knownEndpoints) == 0 {
		return "", ErrNoEndpoints
	}

	// 将已知端点转换为切片
	var endpoints []string
	for endpoint := range d.knownEndpoints {
		conn, ok := d.client.conns[endpoint]
		// 只选择健康的连接
		if ok && conn.isHealthy() {
			endpoints = append(endpoints, endpoint)
		}
	}

	if len(endpoints) == 0 {
		return "", ErrNoEndpoints
	}

	// 对于键特定的请求，使用一致性哈希
	return d.consistentHashEndpoint(endpoints, key)
}

// 随机选择节点
func (d *Discovery) randomEndpoint(endpoints []string) (string, error) {
	idx := rand.Intn(len(endpoints))
	return endpoints[idx], nil
}

// 轮询选择节点
func (d *Discovery) roundRobinEndpoint(endpoints []string) (string, error) {
	d.rrMutex.Lock()
	defer d.rrMutex.Unlock()

	if d.currentIndex >= len(endpoints) {
		d.currentIndex = 0
	}

	endpoint := endpoints[d.currentIndex]
	d.currentIndex++

	return endpoint, nil
}

// 加权选择节点（根据性能）
func (d *Discovery) weightedEndpoint(endpoints []string) (string, error) {
	// 获取节点性能数据
	stats := d.client.getNodeStatsMap()

	// 如果没有统计数据，回退到轮询
	if len(stats) == 0 {
		return d.roundRobinEndpoint(endpoints)
	}

	type weightedNode struct {
		endpoint string
		weight   float64
	}

	var nodes []weightedNode
	var totalWeight float64

	// 计算每个节点的权重（根据响应时间的倒数）
	for _, endpoint := range endpoints {
		if stat, ok := stats[endpoint]; ok && stat.Latency > 0 {
			// 响应时间越低，权重越高
			weight := 1000.0 / float64(stat.Latency)
			nodes = append(nodes, weightedNode{endpoint, weight})
			totalWeight += weight
		} else {
			// 没有统计数据的节点给一个默认权重
			nodes = append(nodes, weightedNode{endpoint, 1.0})
			totalWeight += 1.0
		}
	}

	// 随机选择（加权）
	r := rand.Float64() * totalWeight
	var cumulativeWeight float64

	for _, node := range nodes {
		cumulativeWeight += node.weight
		if r <= cumulativeWeight {
			return node.endpoint, nil
		}
	}

	// 兜底，返回最后一个
	return endpoints[len(endpoints)-1], nil
}

// 最少连接选择节点
func (d *Discovery) leastConnEndpoint(endpoints []string) (string, error) {
	// 在实际实现中，应该跟踪每个连接的活跃请求数
	// 这里简化为使用统计数据中的请求计数
	stats := d.client.getNodeStatsMap()

	// 如果没有统计数据，回退到轮询
	if len(stats) == 0 {
		return d.roundRobinEndpoint(endpoints)
	}

	var minRequests int64 = -1
	var selectedEndpoint string

	for _, endpoint := range endpoints {
		if stat, ok := stats[endpoint]; ok {
			if minRequests == -1 || stat.RequestCount < minRequests {
				minRequests = stat.RequestCount
				selectedEndpoint = endpoint
			}
		}
	}

	if selectedEndpoint != "" {
		return selectedEndpoint, nil
	}

	// 如果没有找到合适的节点，回退到轮询
	return d.roundRobinEndpoint(endpoints)
}

// 一致性哈希选择节点
func (d *Discovery) consistentHashEndpoint(endpoints []string, key string) (string, error) {
	if key == "" {
		// 如果没有提供键，回退到轮询
		return d.roundRobinEndpoint(endpoints)
	}

	// 简化版一致性哈希实现
	// 在实际项目中，应该使用更复杂的一致性哈希算法和虚拟节点
	h := fnv32(key)
	idx := int(h) % len(endpoints)
	return endpoints[idx], nil
}

// FNV-1a 哈希函数（32位）
func fnv32(s string) uint32 {
	const prime32 = uint32(16777619)
	hash := uint32(2166136261)

	for i := 0; i < len(s); i++ {
		hash ^= uint32(s[i])
		hash *= prime32
	}

	return hash
}

// EnableDiscovery 在客户端启用节点发现
func (c *Client) EnableDiscovery(config DiscoveryConfig) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	// 创建并启动发现服务
	discovery := newDiscovery(c, config)
	if err := discovery.Start(); err != nil {
		return err
	}

	c.discovery = discovery
	return nil
}

// 获取所有节点的统计信息映射
func (c *Client) getNodeStatsMap() map[string]NodeStats {
	// 如果没有启用监控，返回空映射
	if c.monitoring == nil {
		return make(map[string]NodeStats)
	}

	stats := c.monitoring.GetClusterStats()
	return stats.NodeStats
}
