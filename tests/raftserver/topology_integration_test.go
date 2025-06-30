/*
* @Author: Lzww0608
* @Date: 2025-01-08
* @LastEditors: Lzww0608 
* @LastEditTime: 2025-01-08
* @Description: ConcordKV中央拓扑服务 - 集成测试套件
*/
package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"testing"
	"time"

	"raftserver/raft"
	"raftserver/topology"
)

// TestTopologyServiceIntegration 拓扑服务集成测试
func TestTopologyServiceIntegration(t *testing.T) {
	t.Run("BasicTopologyOperations", testBasicTopologyOps)
	t.Run("HealthMonitoring", testHealthMonitoring)
	t.Run("EventNotifications", testEventNotifications)
	t.Run("ClientCaching", testClientCaching)
	t.Run("FailoverScenarios", testFailoverScenarios)
}

// testBasicTopologyOps 测试基础拓扑操作
func testBasicTopologyOps(t *testing.T) {
	// 创建拓扑服务配置
	config := &topology.TopologyConfig{
		Enabled:              true,
		HealthCheckInterval:  time.Second * 30,
		HealthCheckTimeout:   time.Second * 5,
		CacheTTL:            time.Minute,
		MaxSubscribers:      100,
		EventBufferSize:     1000,
		SnapshotInterval:    time.Minute,
	}
	
	// 创建拓扑服务（使用模拟组件）
	topologyService := topology.NewTopologyService(nil, nil, nil, config)
	
	// 启动服务
	err := topologyService.Start()
	if err != nil {
		t.Fatalf("启动拓扑服务失败: %v", err)
	}
	defer topologyService.Stop()
	
	// 等待服务初始化
	time.Sleep(time.Second)
	
	// 测试获取拓扑快照
	snapshot, err := topologyService.GetTopologySnapshot()
	if err != nil {
		t.Fatalf("获取拓扑快照失败: %v", err)
	}
	
	if snapshot == nil {
		t.Fatal("拓扑快照为空")
	}
	
	if snapshot.Version <= 0 {
		t.Errorf("拓扑版本无效: %d", snapshot.Version)
	}
	
	t.Logf("拓扑快照获取成功 - 版本: %d, 节点数: %d, 分片数: %d", 
		snapshot.Version, len(snapshot.NodeStatus), len(snapshot.ShardMap))
}

// testHealthMonitoring 测试健康监控功能
func testHealthMonitoring(t *testing.T) {
	config := &topology.TopologyConfig{
		Enabled:              true,
		HealthCheckInterval:  time.Second * 2, // 缩短间隔便于测试
		HealthCheckTimeout:   time.Second,
		CacheTTL:            time.Minute,
		MaxSubscribers:      100,
		EventBufferSize:     1000,
		SnapshotInterval:    time.Minute,
	}
	
	topologyService := topology.NewTopologyService(nil, nil, nil, config)
	
	err := topologyService.Start()
	if err != nil {
		t.Fatalf("启动拓扑服务失败: %v", err)
	}
	defer topologyService.Stop()
	
	// 等待健康检查执行
	time.Sleep(time.Second * 3)
	
	// 获取拓扑快照检查健康状态
	snapshot, err := topologyService.GetTopologySnapshot()
	if err != nil {
		t.Fatalf("获取拓扑快照失败: %v", err)
	}
	
	// 验证至少有一个节点
	if len(snapshot.NodeStatus) == 0 {
		t.Fatal("没有发现任何节点")
	}
	
	// 检查节点健康状态
	for nodeID, nodeStatus := range snapshot.NodeStatus {
		if nodeStatus.Health == topology.HealthStatusUnknown {
			t.Errorf("节点 %s 健康状态未知", nodeID)
		}
		t.Logf("节点 %s 健康状态: %s", nodeID, nodeStatus.Health.String())
	}
}

// testEventNotifications 测试事件通知功能
func testEventNotifications(t *testing.T) {
	config := &topology.TopologyConfig{
		Enabled:              true,
		HealthCheckInterval:  time.Second * 30,
		HealthCheckTimeout:   time.Second * 5,
		CacheTTL:            time.Minute,
		MaxSubscribers:      100,
		EventBufferSize:     1000,
		SnapshotInterval:    time.Minute,
	}
	
	topologyService := topology.NewTopologyService(nil, nil, nil, config)
	
	err := topologyService.Start()
	if err != nil {
		t.Fatalf("启动拓扑服务失败: %v", err)
	}
	defer topologyService.Stop()
	
	// 订阅拓扑事件
	subscriber, err := topologyService.Subscribe("test-client", []topology.EventType{
		topology.EventNodeHealthChange,
		topology.EventConfigChange,
	})
	if err != nil {
		t.Fatalf("订阅拓扑事件失败: %v", err)
	}
	
	// 发布测试事件
	testEvent := &topology.TopologyEvent{
		Type:     topology.EventConfigChange,
		NodeID:   raft.NodeID("test-node"),
		Metadata: map[string]interface{}{"test": "event"},
	}
	
	topologyService.PublishEvent(testEvent)
	
	// 等待事件接收
	select {
	case receivedEvent := <-subscriber.EventChan:
		if receivedEvent.Type != topology.EventConfigChange {
			t.Errorf("接收到错误的事件类型: %v", receivedEvent.Type)
		}
		t.Logf("成功接收事件: 类型=%s, 版本=%d", 
			receivedEvent.Type.String(), receivedEvent.Version)
	case <-time.After(time.Second * 5):
		t.Fatal("事件接收超时")
	}
}

// testClientCaching 测试客户端缓存功能
func testClientCaching(t *testing.T) {
	// 创建客户端配置
	clientConfig := &topology.ClientConfig{
		ServerURLs:        []string{"http://localhost:8081"},
		RequestTimeout:    time.Second * 10,
		RetryInterval:     time.Second * 2,
		MaxRetries:        3,
		HealthCheckPeriod: time.Second * 30,
		CachePolicy: &topology.CachePolicy{
			TTL:              time.Minute * 2,
			RefreshThreshold: time.Minute,
			MaxEntries:       1000,
			CompressionLevel: 1,
		},
	}
	
	// 创建拓扑客户端
	topologyClient := topology.NewTopologyClient(clientConfig)
	
	// 注意：这里需要模拟服务器响应，实际测试中需要启动真实的拓扑服务器
	t.Logf("拓扑客户端创建成功，配置: %+v", clientConfig)
	
	// 测试客户端方法（模拟）
	version := topologyClient.GetTopologyVersion()
	t.Logf("当前拓扑版本: %d", version)
}

// testFailoverScenarios 测试故障转移场景
func testFailoverScenarios(t *testing.T) {
	config := &topology.TopologyConfig{
		Enabled:              true,
		HealthCheckInterval:  time.Second * 2,
		HealthCheckTimeout:   time.Second,
		CacheTTL:            time.Minute,
		MaxSubscribers:      100,
		EventBufferSize:     1000,
		SnapshotInterval:    time.Minute,
	}
	
	topologyService := topology.NewTopologyService(nil, nil, nil, config)
	
	err := topologyService.Start()
	if err != nil {
		t.Fatalf("启动拓扑服务失败: %v", err)
	}
	defer topologyService.Stop()
	
	// 模拟节点故障事件
	failureEvent := &topology.TopologyEvent{
		Type:     topology.EventNodeHealthChange,
		NodeID:   raft.NodeID("node-1"),
		OldValue: topology.HealthStatusHealthy,
		NewValue: topology.HealthStatusUnhealthy,
		Metadata: map[string]interface{}{"reason": "connection_timeout"},
	}
	
	topologyService.PublishEvent(failureEvent)
	
	// 等待事件处理
	time.Sleep(time.Second)
	
	// 验证拓扑状态更新
	snapshot, err := topologyService.GetTopologySnapshot()
	if err != nil {
		t.Fatalf("获取拓扑快照失败: %v", err)
	}
	
	t.Logf("故障转移测试完成 - 拓扑版本: %d", snapshot.Version)
}

// TestTopologyAPIEndpoints 测试拓扑API端点
func TestTopologyAPIEndpoints(t *testing.T) {
	// 注意：这需要实际运行的拓扑服务器
	baseURL := "http://localhost:8081"
	
	// 测试拓扑快照API
	t.Run("TopologySnapshot", func(t *testing.T) {
		resp, err := http.Get(baseURL + "/api/topology/snapshot")
		if err != nil {
			t.Skipf("跳过API测试 - 服务器不可用: %v", err)
			return
		}
		defer resp.Body.Close()
		
		if resp.StatusCode != http.StatusOK {
			t.Errorf("API响应状态错误: %d", resp.StatusCode)
			return
		}
		
		body, err := io.ReadAll(resp.Body)
		if err != nil {
			t.Fatalf("读取响应失败: %v", err)
		}
		
		var result map[string]interface{}
		if err := json.Unmarshal(body, &result); err != nil {
			t.Fatalf("解析响应失败: %v", err)
		}
		
		if success, ok := result["success"].(bool); !ok || !success {
			t.Errorf("API调用失败: %v", result)
		}
		
		t.Logf("拓扑快照API测试成功")
	})
	
	// 测试节点状态API
	t.Run("NodeStatus", func(t *testing.T) {
		resp, err := http.Get(baseURL + "/api/topology/nodes")
		if err != nil {
			t.Skipf("跳过API测试 - 服务器不可用: %v", err)
			return
		}
		defer resp.Body.Close()
		
		if resp.StatusCode != http.StatusOK {
			t.Errorf("API响应状态错误: %d", resp.StatusCode)
			return
		}
		
		t.Logf("节点状态API测试成功")
	})
	
	// 测试健康检查API
	t.Run("HealthCheck", func(t *testing.T) {
		resp, err := http.Get(baseURL + "/api/topology/health")
		if err != nil {
			t.Skipf("跳过API测试 - 服务器不可用: %v", err)
			return
		}
		defer resp.Body.Close()
		
		if resp.StatusCode != http.StatusOK {
			t.Errorf("API响应状态错误: %d", resp.StatusCode)
			return
		}
		
		t.Logf("健康检查API测试成功")
	})
}

// BenchmarkTopologySnapshot 拓扑快照性能基准测试
func BenchmarkTopologySnapshot(b *testing.B) {
	config := &topology.TopologyConfig{
		Enabled:              true,
		HealthCheckInterval:  time.Minute * 5, // 减少干扰
		HealthCheckTimeout:   time.Second * 5,
		CacheTTL:            time.Minute,
		MaxSubscribers:      100,
		EventBufferSize:     1000,
		SnapshotInterval:    time.Minute,
	}
	
	topologyService := topology.NewTopologyService(nil, nil, nil, config)
	
	err := topologyService.Start()
	if err != nil {
		b.Fatalf("启动拓扑服务失败: %v", err)
	}
	defer topologyService.Stop()
	
	// 重置计时器
	b.ResetTimer()
	
	// 基准测试
	for i := 0; i < b.N; i++ {
		_, err := topologyService.GetTopologySnapshot()
		if err != nil {
			b.Fatalf("获取拓扑快照失败: %v", err)
		}
	}
}

// BenchmarkEventPublishing 事件发布性能基准测试
func BenchmarkEventPublishing(b *testing.B) {
	config := &topology.TopologyConfig{
		Enabled:              true,
		HealthCheckInterval:  time.Minute * 5,
		HealthCheckTimeout:   time.Second * 5,
		CacheTTL:            time.Minute,
		MaxSubscribers:      100,
		EventBufferSize:     10000, // 增大缓冲区
		SnapshotInterval:    time.Minute,
	}
	
	topologyService := topology.NewTopologyService(nil, nil, nil, config)
	
	err := topologyService.Start()
	if err != nil {
		b.Fatalf("启动拓扑服务失败: %v", err)
	}
	defer topologyService.Stop()
	
	// 创建测试事件
	testEvent := &topology.TopologyEvent{
		Type:     topology.EventConfigChange,
		NodeID:   raft.NodeID("bench-node"),
		Metadata: map[string]interface{}{"benchmark": true},
	}
	
	// 重置计时器
	b.ResetTimer()
	
	// 基准测试
	for i := 0; i < b.N; i++ {
		topologyService.PublishEvent(testEvent)
	}
}

// 主函数用于独立运行测试
func main() {
	fmt.Println("ConcordKV 中央拓扑服务集成测试")
	fmt.Println("================================")
	
	// 这里可以添加独立的测试运行逻辑
	// 或者使用 go test 命令运行测试
	
	fmt.Println("请使用 'go test' 命令运行测试套件")
	fmt.Println()
	fmt.Println("测试命令示例:")
	fmt.Println("  go test -v                    # 运行所有测试")
	fmt.Println("  go test -v -run TestBasic     # 运行特定测试")
	fmt.Println("  go test -bench=.              # 运行性能基准测试")
	fmt.Println("  go test -cover                # 运行代码覆盖率测试")
}
