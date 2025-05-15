package concord

import (
	"encoding/json"
	"fmt"
	"sync"
	"time"
)

// HealthStatus 表示节点健康状态
type HealthStatus string

const (
	// 节点状态常量
	StatusHealthy   HealthStatus = "健康"
	StatusDegraded  HealthStatus = "性能下降"
	StatusUnhealthy HealthStatus = "不健康"
	StatusUnknown   HealthStatus = "未知"
)

// NodeStats 节点统计信息
type NodeStats struct {
	Endpoint     string       `json:"endpoint"`
	Status       HealthStatus `json:"status"`
	Latency      int64        `json:"latency_ms"` // 毫秒
	LastChecked  time.Time    `json:"last_checked"`
	SuccessRate  float64      `json:"success_rate"` // 0-1
	RequestCount int64        `json:"request_count"`
	ErrorCount   int64        `json:"error_count"`
}

// ClusterStats 集群统计信息
type ClusterStats struct {
	TotalNodes    int                  `json:"total_nodes"`
	HealthyNodes  int                  `json:"healthy_nodes"`
	NodeStats     map[string]NodeStats `json:"node_stats"`
	LastUpdated   time.Time            `json:"last_updated"`
	AvgLatency    int64                `json:"avg_latency_ms"`
	TotalRequests int64                `json:"total_requests"`
}

// Monitoring 监控系统
type Monitoring struct {
	client     *Client
	mu         sync.RWMutex
	nodeStats  map[string]NodeStats
	interval   time.Duration
	stopCh     chan struct{}
	isRunning  bool
	lastUpdate time.Time
}

// 创建新的监控实例
func newMonitoring(client *Client, checkInterval time.Duration) *Monitoring {
	if checkInterval == 0 {
		checkInterval = 30 * time.Second
	}

	mon := &Monitoring{
		client:    client,
		nodeStats: make(map[string]NodeStats),
		interval:  checkInterval,
		stopCh:    make(chan struct{}),
	}

	// 初始化节点状态
	for _, endpoint := range client.config.Endpoints {
		mon.nodeStats[endpoint] = NodeStats{
			Endpoint:     endpoint,
			Status:       StatusUnknown,
			LastChecked:  time.Now(),
			SuccessRate:  1.0,
			RequestCount: 0,
			ErrorCount:   0,
		}
	}

	return mon
}

// 启动监控
func (m *Monitoring) Start() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.isRunning {
		return nil
	}

	m.isRunning = true
	go m.monitorLoop()
	return nil
}

// 停止监控
func (m *Monitoring) Stop() {
	m.mu.Lock()
	defer m.mu.Unlock()

	if !m.isRunning {
		return
	}

	close(m.stopCh)
	m.isRunning = false
}

// 获取集群统计信息
func (m *Monitoring) GetClusterStats() ClusterStats {
	m.mu.RLock()
	defer m.mu.RUnlock()

	stats := ClusterStats{
		TotalNodes:  len(m.nodeStats),
		NodeStats:   make(map[string]NodeStats),
		LastUpdated: m.lastUpdate,
	}

	var totalLatency int64
	var totalRequests int64

	for endpoint, nodeStat := range m.nodeStats {
		stats.NodeStats[endpoint] = nodeStat
		if nodeStat.Status == StatusHealthy {
			stats.HealthyNodes++
		}
		totalLatency += nodeStat.Latency
		totalRequests += nodeStat.RequestCount
	}

	if len(m.nodeStats) > 0 {
		stats.AvgLatency = totalLatency / int64(len(m.nodeStats))
	}
	stats.TotalRequests = totalRequests

	return stats
}

// 获取单个节点统计信息
func (m *Monitoring) GetNodeStats(endpoint string) (NodeStats, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	stats, ok := m.nodeStats[endpoint]
	return stats, ok
}

// 检查所有节点健康状态
func (m *Monitoring) checkAllNodes() {
	m.mu.Lock()
	defer m.mu.Unlock()

	for endpoint := range m.nodeStats {
		m.checkNodeHealth(endpoint)
	}

	m.lastUpdate = time.Now()
}

// 检查单个节点健康状态
func (m *Monitoring) checkNodeHealth(endpoint string) {
	conn, ok := m.client.conns[endpoint]
	if !ok {
		m.updateNodeStatus(endpoint, StatusUnknown, 0)
		return
	}

	start := time.Now()

	// 发送健康检查请求
	req := request{
		Type: "HEALTH",
	}

	resp, err := conn.doRequest(req)
	latency := time.Since(start).Milliseconds()

	// 更新节点统计信息
	stats := m.nodeStats[endpoint]
	stats.LastChecked = time.Now()
	stats.Latency = latency
	stats.RequestCount++

	if err != nil || !resp.Success {
		stats.ErrorCount++
		stats.SuccessRate = float64(stats.RequestCount-stats.ErrorCount) / float64(stats.RequestCount)

		// 根据错误率确定节点状态
		if stats.SuccessRate < 0.5 {
			stats.Status = StatusUnhealthy
		} else {
			stats.Status = StatusDegraded
		}
	} else {
		stats.SuccessRate = float64(stats.RequestCount-stats.ErrorCount) / float64(stats.RequestCount)

		// 根据延迟确定节点状态
		if latency < 100 {
			stats.Status = StatusHealthy
		} else if latency < 500 {
			stats.Status = StatusDegraded
		} else {
			stats.Status = StatusUnhealthy
		}
	}

	m.nodeStats[endpoint] = stats
}

// 更新节点状态
func (m *Monitoring) updateNodeStatus(endpoint string, status HealthStatus, latency int64) {
	stats, ok := m.nodeStats[endpoint]
	if !ok {
		stats = NodeStats{
			Endpoint: endpoint,
		}
	}

	stats.Status = status
	stats.Latency = latency
	stats.LastChecked = time.Now()

	m.nodeStats[endpoint] = stats
}

// 监控循环
func (m *Monitoring) monitorLoop() {
	ticker := time.NewTicker(m.interval)
	defer ticker.Stop()

	// 立即进行一次检查
	m.checkAllNodes()

	for {
		select {
		case <-ticker.C:
			m.checkAllNodes()
		case <-m.stopCh:
			return
		}
	}
}

// 启用监控
func (c *Client) EnableMonitoring(interval time.Duration) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	// 创建并启动监控
	monitoring := newMonitoring(c, interval)
	if err := monitoring.Start(); err != nil {
		return err
	}

	return nil
}

// 获取集群健康状态
func (c *Client) GetClusterHealth() (*ClusterStats, error) {
	// 构建请求
	req := request{
		Type: "CLUSTER_HEALTH",
	}

	resp, err := c.doRequestWithRetry(req)
	if err != nil {
		return nil, err
	}

	if !resp.Success {
		return nil, fmt.Errorf("获取集群健康状态失败: %s", resp.Error)
	}

	// 解析响应
	var stats ClusterStats
	if err := json.Unmarshal([]byte(resp.Value), &stats); err != nil {
		return nil, fmt.Errorf("解析集群健康状态失败: %w", err)
	}

	return &stats, nil
}
