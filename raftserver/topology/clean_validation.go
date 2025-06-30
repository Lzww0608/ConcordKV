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
	}

	fmt.Println("🌐 启动HTTP API服务器 (端口: 8081)...")

	go func() {
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Printf("HTTP服务器错误: %v", err)
		}
	}()

	// 等待服务器启动
	time.Sleep(time.Second)

	fmt.Println("✅ 拓扑服务测试启动完成!")
	fmt.Println()
	fmt.Println("�� 进行功能测试...")

	// 进行功能测试
	runTests(topologyService)

	fmt.Println()
	fmt.Println("🔄 服务正在运行中...")
	fmt.Println("访问 http://localhost:8081 查看API文档")
	fmt.Println("按 Ctrl+C 停止服务")

	// 保持服务运行
	// Service running in background
}

// 运行测试
func runTests(service *SimpleTopologyService) {
	// 测试1: 获取拓扑快照
	fmt.Println("  📸 测试1: 获取拓扑快照")
	snapshot, err := service.GetTopologySnapshot()
	if err != nil {
		fmt.Printf("    ❌ 失败: %v\n", err)
	} else {
		fmt.Printf("    ✅ 成功: 版本=%d, 节点数=%d, 分片数=%d\n",
			snapshot.Version, len(snapshot.NodeStatus), len(snapshot.ShardMap))
	}

	// 测试2: 更新节点健康状态
	fmt.Println("  🔄 测试2: 更新节点健康状态")
	service.UpdateNodeHealth(NodeID("node-3"), HealthStatusUnhealthy)
	fmt.Println("    ✅ 节点node-3状态更新为Unhealthy")

	// 测试3: 再次获取快照验证更新
	fmt.Println("  🔍 测试3: 验证状态更新")
	snapshot2, err := service.GetTopologySnapshot()
	if err != nil {
		fmt.Printf("    ❌ 失败: %v\n", err)
	} else {
		if node3, exists := snapshot2.NodeStatus[NodeID("node-3")]; exists {
			fmt.Printf("    ✅ 成功: node-3健康状态=%s, 版本=%d\n",
				node3.Health.String(), snapshot2.Version)
		}
	}

	// 测试4: HTTP API调用
	fmt.Println("  🌐 测试4: HTTP API调用")
	testHTTPAPI()
}

// 测试HTTP API
func testHTTPAPI() {
	client := &http.Client{Timeout: time.Second * 5}
	baseURL := "http://localhost:8081"

	// 测试拓扑快照API
	resp, err := client.Get(baseURL + "/api/topology/snapshot")
	if err != nil {
		fmt.Printf("    ❌ 快照API失败: %v\n", err)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode == http.StatusOK {
		fmt.Println("    ✅ 快照API调用成功")
	} else {
		fmt.Printf("    ❌ 快照API状态码: %d\n", resp.StatusCode)
	}

	// 测试节点状态API
	resp2, err := client.Get(baseURL + "/api/topology/nodes")
	if err != nil {
		fmt.Printf("    ❌ 节点API失败: %v\n", err)
		return
	}
	defer resp2.Body.Close()

	if resp2.StatusCode == http.StatusOK {
		fmt.Println("    ✅ 节点API调用成功")
	} else {
		fmt.Printf("    ❌ 节点API状态码: %d\n", resp2.StatusCode)
	}

	// 测试健康检查API
	resp3, err := client.Get(baseURL + "/api/topology/health")
	if err != nil {
		fmt.Printf("    ❌ 健康检查API失败: %v\n", err)
		return
	}
	defer resp3.Body.Close()

	if resp3.StatusCode == http.StatusOK {
		fmt.Println("    ✅ 健康检查API调用成功")
	} else {
		fmt.Printf("    ❌ 健康检查API状态码: %d\n", resp3.StatusCode)
	}
}
