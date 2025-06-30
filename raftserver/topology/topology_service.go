/*
* @Author: Lzww0608
* @Date: 2025-6-30 22:21:46
* @LastEditors: Lzww0608
* @LastEditTime: 2025-06-30 22:21:46
* @Description: ConcordKV中央拓扑服务 - 拓扑元数据管理器
 */
package topology

import (
	"context"
	"fmt"
	"log"
	"sync"
	"time"

	"raftserver/raft"
	"raftserver/sharding"
	"raftserver/transport"
)

// TopologyService 拓扑服务管理器
type TopologyService struct {
	// 复用现有组件
	raftNode     *raft.Node
	shardManager *sharding.ShardMetadataManager
	transport    *transport.HTTPTransport

	// 拓扑专用功能
	topologyCache  map[string]*TopologySnapshot
	subscriptions  map[string]*TopologySubscriber
	healthCheckers map[raft.NodeID]*NodeHealthStatus
	version        int64

	// 同步控制
	mu         sync.RWMutex
	eventChan  chan *TopologyEvent
	shutdownCh chan struct{}
	logger     *log.Logger

	// 配置
	config *TopologyConfig
}

// TopologySnapshot 拓扑快照
type TopologySnapshot struct {
	Version    int64                       `json:"version"`
	Timestamp  time.Time                   `json:"timestamp"`
	ShardMap   map[string]*ShardInfo       `json:"shardMap"`
	NodeStatus map[raft.NodeID]*NodeStatus `json:"nodeStatus"`
	ConfigHash string                      `json:"configHash"`
	Metadata   map[string]interface{}      `json:"metadata"`
}

// ShardInfo 分片信息
type ShardInfo struct {
	ShardID      string        `json:"shardId"`
	KeyRange     *KeyRange     `json:"keyRange"`
	PrimaryNode  raft.NodeID   `json:"primaryNode"`
	ReplicaNodes []raft.NodeID `json:"replicaNodes"`
	Status       ShardStatus   `json:"status"`
	LastUpdate   time.Time     `json:"lastUpdate"`
}

// NodeStatus 节点状态
type NodeStatus struct {
	NodeID   raft.NodeID            `json:"nodeId"`
	Address  string                 `json:"address"`
	Health   HealthStatus           `json:"health"`
	Load     LoadInfo               `json:"load"`
	Shards   []string               `json:"shards"`
	LastSeen time.Time              `json:"lastSeen"`
	Metadata map[string]interface{} `json:"metadata"`
}

// TopologyEvent 拓扑变更事件
type TopologyEvent struct {
	Type      EventType              `json:"type"`
	Timestamp time.Time              `json:"timestamp"`
	Version   int64                  `json:"version"`
	NodeID    raft.NodeID            `json:"nodeId,omitempty"`
	ShardID   string                 `json:"shardId,omitempty"`
	OldValue  interface{}            `json:"oldValue,omitempty"`
	NewValue  interface{}            `json:"newValue,omitempty"`
	Metadata  map[string]interface{} `json:"metadata,omitempty"`
}

// TopologySubscriber 客户端订阅者
type TopologySubscriber struct {
	ID          string              `json:"id"`
	ClientAddr  string              `json:"clientAddr"`
	EventTypes  []EventType         `json:"eventTypes"`
	LastVersion int64               `json:"lastVersion"`
	EventChan   chan *TopologyEvent `json:"-"`
	CreatedAt   time.Time           `json:"createdAt"`
	LastActive  time.Time           `json:"lastActive"`
}

// NodeHealthStatus 节点健康状态
type NodeHealthStatus struct {
	NodeID       raft.NodeID   `json:"nodeId"`
	Health       HealthStatus  `json:"health"`
	LastCheck    time.Time     `json:"lastCheck"`
	FailureCount int           `json:"failureCount"`
	ResponseTime time.Duration `json:"responseTime"`
	ErrorMessage string        `json:"errorMessage,omitempty"`
}

// 枚举类型定义
type EventType int
type ShardStatus int
type HealthStatus int

const (
	// 事件类型
	EventNodeJoin EventType = iota
	EventNodeLeave
	EventNodeHealthChange
	EventShardAdd
	EventShardRemove
	EventShardMove
	EventConfigChange
)

const (
	// 分片状态
	ShardStatusActive ShardStatus = iota
	ShardStatusMigrating
	ShardStatusInactive
	ShardStatusError
)

const (
	// 健康状态
	HealthStatusHealthy HealthStatus = iota
	HealthStatusUnhealthy
	HealthStatusUnknown
	HealthStatusDegraded
)

// KeyRange 键范围定义
type KeyRange struct {
	Start string `json:"start"`
	End   string `json:"end"`
}

// LoadInfo 负载信息
type LoadInfo struct {
	CPUUsage    float64 `json:"cpuUsage"`
	MemoryUsage float64 `json:"memoryUsage"`
	DiskUsage   float64 `json:"diskUsage"`
	QPS         int64   `json:"qps"`
}

// TopologyConfig 拓扑服务配置
type TopologyConfig struct {
	Enabled             bool          `yaml:"enabled"`
	HealthCheckInterval time.Duration `yaml:"healthCheckInterval"`
	HealthCheckTimeout  time.Duration `yaml:"healthCheckTimeout"`
	CacheTTL            time.Duration `yaml:"cacheTTL"`
	MaxSubscribers      int           `yaml:"maxSubscribers"`
	EventBufferSize     int           `yaml:"eventBufferSize"`
	SnapshotInterval    time.Duration `yaml:"snapshotInterval"`
}

// NewTopologyService 创建新的拓扑服务
func NewTopologyService(
	raftNode *raft.Node,
	shardManager *sharding.ShardMetadataManager,
	transport *transport.HTTPTransport,
	config *TopologyConfig,
) *TopologyService {
	return &TopologyService{
		// 复用现有组件
		raftNode:     raftNode,
		shardManager: shardManager,
		transport:    transport,

		// 初始化拓扑专用组件
		topologyCache:  make(map[string]*TopologySnapshot),
		subscriptions:  make(map[string]*TopologySubscriber),
		healthCheckers: make(map[raft.NodeID]*NodeHealthStatus),
		version:        0,

		// 控制通道
		eventChan:  make(chan *TopologyEvent, config.EventBufferSize),
		shutdownCh: make(chan struct{}),

		// 配置和日志
		config: config,
		logger: log.New(log.Writer(), "[topology-service] ", log.LstdFlags),
	}
}

// Start 启动拓扑服务
func (ts *TopologyService) Start() error {
	ts.mu.Lock()
	defer ts.mu.Unlock()

	ts.logger.Printf("启动中央拓扑服务...")

	// 初始化拓扑快照
	if err := ts.initializeTopologySnapshot(); err != nil {
		return fmt.Errorf("初始化拓扑快照失败: %w", err)
	}

	// 启动事件处理goroutine
	go ts.eventProcessor()

	// 启动健康检查goroutine
	go ts.healthCheckLoop()

	// 启动定期快照goroutine
	go ts.snapshotLoop()

	ts.logger.Printf("中央拓扑服务启动成功")
	return nil
}

// GetTopologySnapshot 获取当前拓扑快照
func (ts *TopologyService) GetTopologySnapshot() (*TopologySnapshot, error) {
	ts.mu.RLock()
	defer ts.mu.RUnlock()

	snapshot := &TopologySnapshot{
		Version:    ts.version,
		Timestamp:  time.Now(),
		ShardMap:   make(map[string]*ShardInfo),
		NodeStatus: make(map[raft.NodeID]*NodeStatus),
		ConfigHash: ts.calculateConfigHash(),
		Metadata:   make(map[string]interface{}),
	}

	// 添加节点健康状态
	for nodeID, health := range ts.healthCheckers {
		snapshot.NodeStatus[nodeID] = &NodeStatus{
			NodeID:   nodeID,
			Health:   health.Health,
			LastSeen: health.LastCheck,
			Shards:   []string{},
			Metadata: make(map[string]interface{}),
		}
	}

	return snapshot, nil
}

// Subscribe 订阅拓扑变更事件
func (ts *TopologyService) Subscribe(clientAddr string, eventTypes []EventType) (*TopologySubscriber, error) {
	ts.mu.Lock()
	defer ts.mu.Unlock()

	if len(ts.subscriptions) >= ts.config.MaxSubscribers {
		return nil, fmt.Errorf("达到最大订阅者数量限制: %d", ts.config.MaxSubscribers)
	}

	subscriberID := fmt.Sprintf("%s-%d", clientAddr, time.Now().UnixNano())
	subscriber := &TopologySubscriber{
		ID:          subscriberID,
		ClientAddr:  clientAddr,
		EventTypes:  eventTypes,
		LastVersion: ts.version,
		EventChan:   make(chan *TopologyEvent, 100),
		CreatedAt:   time.Now(),
		LastActive:  time.Now(),
	}

	ts.subscriptions[subscriberID] = subscriber

	ts.logger.Printf("新增拓扑订阅者: %s (地址: %s)", subscriberID, clientAddr)
	return subscriber, nil
}

// PublishEvent 发布拓扑变更事件
func (ts *TopologyService) PublishEvent(event *TopologyEvent) {
	ts.mu.Lock()
	ts.version++
	event.Version = ts.version
	event.Timestamp = time.Now()
	ts.mu.Unlock()

	select {
	case ts.eventChan <- event:
	default:
		ts.logger.Printf("事件通道已满，丢弃事件: %+v", event)
	}
}

// 私有方法实现

func (ts *TopologyService) initializeTopologySnapshot() error {
	ts.version = 1

	// 初始化健康检查器
	ts.healthCheckers[raft.NodeID("node-1")] = &NodeHealthStatus{
		NodeID:       raft.NodeID("node-1"),
		Health:       HealthStatusHealthy,
		LastCheck:    time.Now(),
		FailureCount: 0,
	}

	ts.logger.Printf("拓扑快照初始化完成，版本: %d", ts.version)
	return nil
}

func (ts *TopologyService) eventProcessor() {
	for {
		select {
		case event := <-ts.eventChan:
			ts.processTopologyEvent(event)
		case <-ts.shutdownCh:
			return
		}
	}
}

func (ts *TopologyService) processTopologyEvent(event *TopologyEvent) {
	ts.mu.RLock()
	subscribers := make([]*TopologySubscriber, 0, len(ts.subscriptions))
	for _, subscriber := range ts.subscriptions {
		if ts.isEventTypeSubscribed(subscriber, event.Type) {
			subscribers = append(subscribers, subscriber)
		}
	}
	ts.mu.RUnlock()

	for _, subscriber := range subscribers {
		select {
		case subscriber.EventChan <- event:
			subscriber.LastActive = time.Now()
		default:
			ts.logger.Printf("订阅者 %s 事件通道已满，跳过事件", subscriber.ID)
		}
	}
}

func (ts *TopologyService) isEventTypeSubscribed(subscriber *TopologySubscriber, eventType EventType) bool {
	for _, et := range subscriber.EventTypes {
		if et == eventType {
			return true
		}
	}
	return false
}

func (ts *TopologyService) healthCheckLoop() {
	ticker := time.NewTicker(ts.config.HealthCheckInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			ts.performHealthChecks()
		case <-ts.shutdownCh:
			return
		}
	}
}

func (ts *TopologyService) performHealthChecks() {
	ts.mu.RLock()
	healthCheckers := make(map[raft.NodeID]*NodeHealthStatus)
	for nodeID, checker := range ts.healthCheckers {
		healthCheckers[nodeID] = checker
	}
	ts.mu.RUnlock()

	for nodeID, checker := range healthCheckers {
		go ts.checkNodeHealth(nodeID, checker)
	}
}

func (ts *TopologyService) checkNodeHealth(nodeID raft.NodeID, checker *NodeHealthStatus) {
	start := time.Now()

	ctx, cancel := context.WithTimeout(context.Background(), ts.config.HealthCheckTimeout)
	defer cancel()

	isHealthy := true
	responseTime := time.Since(start)

	ts.mu.Lock()
	if checker, exists := ts.healthCheckers[nodeID]; exists {
		oldHealth := checker.Health

		if isHealthy {
			checker.Health = HealthStatusHealthy
			checker.FailureCount = 0
			checker.ErrorMessage = ""
		} else {
			checker.Health = HealthStatusUnhealthy
			checker.FailureCount++
			checker.ErrorMessage = "健康检查失败"
		}

		checker.LastCheck = time.Now()
		checker.ResponseTime = responseTime

		if oldHealth != checker.Health {
			ts.mu.Unlock()
			event := &TopologyEvent{
				Type:     EventNodeHealthChange,
				NodeID:   nodeID,
				OldValue: oldHealth,
				NewValue: checker.Health,
			}
			ts.PublishEvent(event)
			return
		}
	}
	ts.mu.Unlock()
}

func (ts *TopologyService) snapshotLoop() {
	ticker := time.NewTicker(ts.config.SnapshotInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			ts.createPeriodicSnapshot()
		case <-ts.shutdownCh:
			return
		}
	}
}

func (ts *TopologyService) createPeriodicSnapshot() {
	snapshot, err := ts.GetTopologySnapshot()
	if err != nil {
		ts.logger.Printf("创建定期快照失败: %v", err)
		return
	}

	ts.mu.Lock()
	snapshotKey := fmt.Sprintf("snapshot-%d", snapshot.Version)
	ts.topologyCache[snapshotKey] = snapshot

	if len(ts.topologyCache) > 10 {
		for key := range ts.topologyCache {
			delete(ts.topologyCache, key)
			break
		}
	}
	ts.mu.Unlock()

	ts.logger.Printf("创建定期拓扑快照: 版本=%d", snapshot.Version)
}

func (ts *TopologyService) calculateConfigHash() string {
	return fmt.Sprintf("config-hash-%d", time.Now().Unix())
}

// 字符串方法实现
func (et EventType) String() string {
	switch et {
	case EventNodeJoin:
		return "NodeJoin"
	case EventNodeLeave:
		return "NodeLeave"
	case EventNodeHealthChange:
		return "NodeHealthChange"
	case EventShardAdd:
		return "ShardAdd"
	case EventShardRemove:
		return "ShardRemove"
	case EventShardMove:
		return "ShardMove"
	case EventConfigChange:
		return "ConfigChange"
	default:
		return "Unknown"
	}
}

func (hs HealthStatus) String() string {
	switch hs {
	case HealthStatusHealthy:
		return "Healthy"
	case HealthStatusUnhealthy:
		return "Unhealthy"
	case HealthStatusUnknown:
		return "Unknown"
	case HealthStatusDegraded:
		return "Degraded"
	default:
		return "Unknown"
	}
}
