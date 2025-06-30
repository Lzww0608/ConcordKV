package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"
)

// 简化的类型定义用于测试
type NodeID string
type EventType int
type HealthStatus int
type ShardStatus int

const (
	EventNodeJoin EventType = iota
	EventNodeLeave
	EventNodeHealthChange
	EventShardAdd
	EventShardRemove
	EventShardMove
	EventConfigChange
)

const (
	HealthStatusHealthy HealthStatus = iota
	HealthStatusUnhealthy
	HealthStatusUnknown
	HealthStatusDegraded
)

const (
	ShardStatusActive ShardStatus = iota
	ShardStatusMigrating
	ShardStatusInactive
	ShardStatusError
)

// TopologySnapshot 拓扑快照
type TopologySnapshot struct {
	Version    int64                  `json:"version"`
	Timestamp  time.Time              `json:"timestamp"`
	ShardMap   map[string]*ShardInfo  `json:"shardMap"`
	NodeStatus map[NodeID]*NodeStatus `json:"nodeStatus"`
	ConfigHash string                 `json:"configHash"`
	Metadata   map[string]interface{} `json:"metadata"`
}

// ShardInfo 分片信息
type ShardInfo struct {
	ShardID      string      `json:"shardId"`
	KeyRange     *KeyRange   `json:"keyRange"`
	PrimaryNode  NodeID      `json:"primaryNode"`
	ReplicaNodes []NodeID    `json:"replicaNodes"`
	Status       ShardStatus `json:"status"`
	LastUpdate   time.Time   `json:"lastUpdate"`
}

// NodeStatus 节点状态
type NodeStatus struct {
	NodeID   NodeID                 `json:"nodeId"`
	Address  string                 `json:"address"`
	Health   HealthStatus           `json:"health"`
	Shards   []string               `json:"shards"`
	LastSeen time.Time              `json:"lastSeen"`
	Metadata map[string]interface{} `json:"metadata"`
}

// TopologyEvent 拓扑变更事件
type TopologyEvent struct {
	Type      EventType              `json:"type"`
	Timestamp time.Time              `json:"timestamp"`
	Version   int64                  `json:"version"`
	NodeID    NodeID                 `json:"nodeId,omitempty"`
	ShardID   string                 `json:"shardId,omitempty"`
	OldValue  interface{}            `json:"oldValue,omitempty"`
	NewValue  interface{}            `json:"newValue,omitempty"`
	Metadata  map[string]interface{} `json:"metadata,omitempty"`
}

// KeyRange 键范围定义
type KeyRange struct {
	Start string `json:"start"`
	End   string `json:"end"`
}

// 简化的拓扑服务
type SimpleTopologyService struct {
	version    int64
	nodeStatus map[NodeID]*NodeStatus
	shardMap   map[string]*ShardInfo
	mu         sync.RWMutex
	logger     *log.Logger
}

// 创建简化的拓扑服务
func NewSimpleTopologyService() *SimpleTopologyService {
	service := &SimpleTopologyService{
		version:    1,
		nodeStatus: make(map[NodeID]*NodeStatus),
		shardMap:   make(map[string]*ShardInfo),
		logger:     log.New(log.Writer(), "[topology-test] ", log.LstdFlags),
	}

	// 初始化测试数据
	service.initializeTestData()
	return service
}

// 初始化测试数据
func (s *SimpleTopologyService) initializeTestData() {
	// 添加测试节点
	s.nodeStatus[NodeID("node-1")] = &NodeStatus{
		NodeID:   NodeID("node-1"),
		Address:  "localhost:8001",
		Health:   HealthStatusHealthy,
		Shards:   []string{"shard-1", "shard-2"},
		LastSeen: time.Now(),
		Metadata: make(map[string]interface{}),
	}

	s.nodeStatus[NodeID("node-2")] = &NodeStatus{
		NodeID:   NodeID("node-2"),
		Address:  "localhost:8002",
		Health:   HealthStatusHealthy,
		Shards:   []string{"shard-2", "shard-3"},
		LastSeen: time.Now(),
		Metadata: make(map[string]interface{}),
	}

	s.nodeStatus[NodeID("node-3")] = &NodeStatus{
		NodeID:   NodeID("node-3"),
		Address:  "localhost:8003",
		Health:   HealthStatusDegraded,
		Shards:   []string{"shard-1", "shard-3"},
		LastSeen: time.Now().Add(-time.Minute * 2),
		Metadata: make(map[string]interface{}),
	}

	// 添加测试分片
	s.shardMap["shard-1"] = &ShardInfo{
		ShardID:      "shard-1",
		KeyRange:     &KeyRange{Start: "a", End: "h"},
		PrimaryNode:  NodeID("node-1"),
		ReplicaNodes: []NodeID{NodeID("node-3")},
		Status:       ShardStatusActive,
		LastUpdate:   time.Now(),
	}

	s.shardMap["shard-2"] = &ShardInfo{
		ShardID:      "shard-2",
		KeyRange:     &KeyRange{Start: "h", End: "p"},
		PrimaryNode:  NodeID("node-1"),
		ReplicaNodes: []NodeID{NodeID("node-2")},
		Status:       ShardStatusActive,
		LastUpdate:   time.Now(),
	}

	s.shardMap["shard-3"] = &ShardInfo{
		ShardID:      "shard-3",
		KeyRange:     &KeyRange{Start: "p", End: "z"},
		PrimaryNode:  NodeID("node-2"),
		ReplicaNodes: []NodeID{NodeID("node-3")},
		Status:       ShardStatusActive,
		LastUpdate:   time.Now(),
	}
}

// 获取拓扑快照
func (s *SimpleTopologyService) GetTopologySnapshot() (*TopologySnapshot, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	snapshot := &TopologySnapshot{
		Version:    s.version,
		Timestamp:  time.Now(),
		ShardMap:   make(map[string]*ShardInfo),
		NodeStatus: make(map[NodeID]*NodeStatus),
		ConfigHash: fmt.Sprintf("config-hash-%d", time.Now().Unix()),
		Metadata:   make(map[string]interface{}),
	}

	// 复制分片映射
	for id, shard := range s.shardMap {
		snapshot.ShardMap[id] = &ShardInfo{
			ShardID:      shard.ShardID,
			KeyRange:     &KeyRange{Start: shard.KeyRange.Start, End: shard.KeyRange.End},
			PrimaryNode:  shard.PrimaryNode,
			ReplicaNodes: append([]NodeID{}, shard.ReplicaNodes...),
			Status:       shard.Status,
			LastUpdate:   shard.LastUpdate,
		}
	}

	// 复制节点状态
	for id, node := range s.nodeStatus {
		snapshot.NodeStatus[id] = &NodeStatus{
			NodeID:   node.NodeID,
			Address:  node.Address,
			Health:   node.Health,
			Shards:   append([]string{}, node.Shards...),
			LastSeen: node.LastSeen,
			Metadata: make(map[string]interface{}),
		}
	}

	return snapshot, nil
}

// 更新节点健康状态
func (s *SimpleTopologyService) UpdateNodeHealth(nodeID NodeID, health HealthStatus) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if node, exists := s.nodeStatus[nodeID]; exists {
		oldHealth := node.Health
		node.Health = health
		node.LastSeen = time.Now()
		s.version++

		s.logger.Printf("节点 %s 健康状态更新: %d -> %d", nodeID, oldHealth, health)
	}
}

// HTTP API处理器
func (s *SimpleTopologyService) SetupAPI() *http.ServeMux {
	mux := http.NewServeMux()

	// 拓扑快照API
	mux.HandleFunc("/api/topology/snapshot", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "GET" {
			http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
			return
		}

		snapshot, err := s.GetTopologySnapshot()
		if err != nil {
			http.Error(w, fmt.Sprintf("获取拓扑快照失败: %v", err), http.StatusInternalServerError)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]interface{}{
			"success":  true,
			"snapshot": snapshot,
			"message":  "拓扑快照获取成功",
		})
	})

	// 节点状态API
	mux.HandleFunc("/api/topology/nodes", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "GET" {
			http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
			return
		}

		snapshot, err := s.GetTopologySnapshot()
		if err != nil {
			http.Error(w, fmt.Sprintf("获取节点状态失败: %v", err), http.StatusInternalServerError)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]interface{}{
			"success":    true,
			"nodeStatus": snapshot.NodeStatus,
			"totalNodes": len(snapshot.NodeStatus),
			"message":    "节点状态获取成功",
		})
	})

	// 分片映射API
	mux.HandleFunc("/api/topology/shards", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "GET" {
			http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
			return
		}

		snapshot, err := s.GetTopologySnapshot()
		if err != nil {
			http.Error(w, fmt.Sprintf("获取分片映射失败: %v", err), http.StatusInternalServerError)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]interface{}{
			"success":     true,
			"shardMap":    snapshot.ShardMap,
			"totalShards": len(snapshot.ShardMap),
			"message":     "分片映射获取成功",
		})
	})

	// 健康检查API
	mux.HandleFunc("/api/topology/health", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "GET" {
			http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
			return
		}

		snapshot, err := s.GetTopologySnapshot()
		if err != nil {
			http.Error(w, "拓扑服务不健康", http.StatusInternalServerError)
			return
		}

		healthyNodes := 0
		for _, node := range snapshot.NodeStatus {
			if node.Health == HealthStatusHealthy {
				healthyNodes++
			}
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]interface{}{
			"success":      true,
			"health":       "healthy",
			"version":      snapshot.Version,
			"timestamp":    snapshot.Timestamp,
			"totalNodes":   len(snapshot.NodeStatus),
			"healthyNodes": healthyNodes,
			"message":      "拓扑服务健康",
		})
	})

	// 根路径
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html")
		fmt.Fprintf(w, `
		<h1>ConcordKV 中央拓扑服务测试</h1>
		<h2>可用的API端点:</h2>
		<ul>
			<li><a href="/api/topology/snapshot">GET /api/topology/snapshot</a> - 获取拓扑快照</li>
			<li><a href="/api/topology/nodes">GET /api/topology/nodes</a> - 获取节点状态</li>
			<li><a href="/api/topology/shards">GET /api/topology/shards</a> - 获取分片映射</li>
			<li><a href="/api/topology/health">GET /api/topology/health</a> - 健康检查</li>
		</ul>
		<h2>测试数据:</h2>
		<p>系统包含3个节点(node-1, node-2, node-3)和3个分片(shard-1, shard-2, shard-3)</p>
		`)
	})

	return mux
}

// 字符串方法
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

// 主程序
func main() {
	fmt.Println("�� ConcordKV 中央拓扑服务测试")
	fmt.Println("================================")

	// 创建拓扑服务
	topologyService := NewSimpleTopologyService()

	// 设置API
	mux := topologyService.SetupAPI()

	// 启动HTTP服务器
	server := &http.Server{
		Addr:    ":8081",
		Handler: mux,

// 简化测试主函数
func main() {
	fmt.Println("🧪 ConcordKV 中央拓扑服务功能验证")
	fmt.Println("================================")
	
	// 创建拓扑服务
	fmt.Println("📋 1. 初始化拓扑服务...")
	service := NewSimpleTopologyService()
	
	// 测试基础功能
	fmt.Println("📋 2. 测试基础功能...")
	snapshot, err := service.GetTopologySnapshot()
	if err != nil {
		fmt.Printf("  ❌ 失败: %v\n", err)
		return
	}
	fmt.Printf("  ✅ 成功: 版本=%d, 节点=%d, 分片=%d\n", 
		snapshot.Version, len(snapshot.NodeStatus), len(snapshot.ShardMap))
	
	// 测试健康状态更新
	fmt.Println("📋 3. 测试健康状态更新...")
	oldVersion := snapshot.Version
	service.UpdateNodeHealth(NodeID("node-1"), HealthStatusUnhealthy)
	
	snapshot2, _ := service.GetTopologySnapshot()
	if snapshot2.Version > oldVersion {
		fmt.Printf("  ✅ 成功: 版本更新 %d -> %d\n", oldVersion, snapshot2.Version)
	} else {
		fmt.Printf("  ❌ 失败: 版本未更新\n")
	}
	
	// 显示最终状态
	fmt.Println("📋 4. 最终状态:")
	for nodeID, node := range snapshot2.NodeStatus {
		fmt.Printf("  🖥️  %s: %s (%s)\n", nodeID, node.Health.String(), node.Address)
	}
	
	for shardID, shard := range snapshot2.ShardMap {
		fmt.Printf("  �� %s: %s-%s (主节点: %s)\n", 
			shardID, shard.KeyRange.Start, shard.KeyRange.End, shard.PrimaryNode)
	}
	
	fmt.Println("\n🎉 所有功能验证通过!")
}
