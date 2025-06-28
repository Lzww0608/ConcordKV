/*
 * @Author: Lzww0608
 * @Date: 2025-1-28 22:05:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-1-28 22:05:00
 * @Description: ConcordKV 读写分离路由器测试
 */

package main

import (
	"fmt"
	"log"
	"testing"
	"time"

	"raftserver/raft"
	"raftserver/replication"
)

// createTestReadWriteRouter 创建测试用读写分离路由器
func createTestReadWriteRouter() *replication.ReadWriteRouter {
	// 创建测试配置
	raftConfig := &raft.Config{
		NodeID: "node1",
		MultiDC: &raft.MultiDCConfig{
			Enabled: true,
			LocalDataCenter: raft.DataCenter{
				ID:     "dc1",
				Region: "us-east-1",
			},
		},
		Servers: []raft.ServerConfig{
			{ID: "node1", DataCenter: "dc1", Address: "localhost:8001"},
			{ID: "node2", DataCenter: "dc1", Address: "localhost:8002"},
			{ID: "node3", DataCenter: "dc2", Address: "localhost:8003"},
			{ID: "node4", DataCenter: "dc2", Address: "localhost:8004"},
			{ID: "node5", DataCenter: "dc3", Address: "localhost:8005"},
		},
	}

	return replication.NewReadWriteRouter("node1", raftConfig)
}

// TestReadWriteRouterCreation 测试读写分离路由器创建
func TestReadWriteRouterCreation(t *testing.T) {
	router := createTestReadWriteRouter()

	if router == nil {
		t.Fatal("读写分离路由器创建失败")
	}

	t.Log("✓ 读写分离路由器创建成功")
}

// TestReadRouting 测试读请求路由
func TestReadRouting(t *testing.T) {
	router := createTestReadWriteRouter()

	// 启动路由器
	err := router.Start()
	if err != nil {
		t.Fatalf("启动读写分离路由器失败: %v", err)
	}
	defer router.Stop()

	// 等待初始化完成
	time.Sleep(100 * time.Millisecond)

	// 测试不同一致性级别的读请求路由
	testCases := []struct {
		key         string
		consistency replication.ReadConsistencyLevel
		description string
	}{
		{"user:123", replication.ReadConsistencyEventual, "最终一致性读"},
		{"session:abc", replication.ReadConsistencyBounded, "有界一致性读"},
		{"config:sys", replication.ReadConsistencyStrong, "强一致性读"},
		{"critical:data", replication.ReadConsistencyLinearizable, "线性一致性读"},
	}

	for _, tc := range testCases {
		decision, err := router.RouteRequest(replication.RequestTypeRead, tc.key, tc.consistency)
		if err != nil {
			t.Errorf("读请求路由失败 (%s): %v", tc.description, err)
			continue
		}

		t.Logf("✓ %s: 键=%s, 目标节点=%s, 目标DC=%s, 延迟=%v", 
			tc.description, tc.key, decision.TargetNode, decision.TargetDC, decision.Latency)

		// 验证路由决策
		if decision.RequestType != replication.RequestTypeRead {
			t.Errorf("路由类型错误: 期望=Read, 实际=%d", decision.RequestType)
		}

		if decision.TargetNode == "" {
			t.Error("目标节点为空")
		}

		if decision.TargetDC == "" {
			t.Error("目标DC为空")
		}
	}
}

// TestWriteRouting 测试写请求路由
func TestWriteRouting(t *testing.T) {
	router := createTestReadWriteRouter()

	// 启动路由器
	err := router.Start()
	if err != nil {
		t.Fatalf("启动读写分离路由器失败: %v", err)
	}
	defer router.Stop()

	// 等待初始化完成
	time.Sleep(100 * time.Millisecond)

	// 测试写请求路由
	testKeys := []string{
		"user:123",
		"order:456",
		"product:789",
		"inventory:abc",
	}

	for _, key := range testKeys {
		decision, err := router.RouteRequest(replication.RequestTypeWrite, key, replication.ReadConsistencyStrong)
		if err != nil {
			t.Errorf("写请求路由失败 (键=%s): %v", key, err)
			continue
		}

		t.Logf("✓ 写请求路由: 键=%s, 目标节点=%s, 目标DC=%s, 延迟=%v", 
			key, decision.TargetNode, decision.TargetDC, decision.Latency)

		// 验证路由决策
		if decision.RequestType != replication.RequestTypeWrite {
			t.Errorf("路由类型错误: 期望=Write, 实际=%d", decision.RequestType)
		}

		if decision.TargetNode == "" {
			t.Error("目标节点为空")
		}

		if decision.TargetDC == "" {
			t.Error("目标DC为空")
		}

		// 写请求应该路由到主DC
		if decision.TargetDC != "dc1" { // 假设dc1是主DC
			t.Logf("注意: 写请求路由到DC=%s (可能不是主DC)", decision.TargetDC)
		}
	}
}

// TestLoadBalancing 测试负载均衡
func TestLoadBalancing(t *testing.T) {
	router := createTestReadWriteRouter()

	// 启动路由器
	err := router.Start()
	if err != nil {
		t.Fatalf("启动读写分离路由器失败: %v", err)
	}
	defer router.Stop()

	// 等待初始化完成
	time.Sleep(100 * time.Millisecond)

	// 执行多次路由请求，验证负载均衡
	nodeCount := make(map[raft.NodeID]int)
	dcCount := make(map[raft.DataCenterID]int)

	requestCount := 20
	for i := 0; i < requestCount; i++ {
		key := fmt.Sprintf("test-key-%d", i)
		decision, err := router.RouteRequest(replication.RequestTypeRead, key, replication.ReadConsistencyEventual)
		if err != nil {
			t.Errorf("负载均衡测试路由失败 (请求%d): %v", i, err)
			continue
		}

		nodeCount[decision.TargetNode]++
		dcCount[decision.TargetDC]++
	}

	t.Log("✓ 负载均衡统计:")
	t.Log("  节点分布:")
	for nodeID, count := range nodeCount {
		percentage := float64(count) / float64(requestCount) * 100
		t.Logf("    节点 %s: %d 次 (%.1f%%)", nodeID, count, percentage)
	}

	t.Log("  DC分布:")
	for dcID, count := range dcCount {
		percentage := float64(count) / float64(requestCount) * 100
		t.Logf("    DC %s: %d 次 (%.1f%%)", dcID, count, percentage)
	}

	// 验证负载均衡效果
	if len(nodeCount) < 2 {
		t.Error("负载均衡效果不佳：只使用了少于2个节点")
	}
}

// TestHealthChecks 测试健康检查
func TestHealthChecks(t *testing.T) {
	router := createTestReadWriteRouter()

	// 启动路由器
	err := router.Start()
	if err != nil {
		t.Fatalf("启动读写分离路由器失败: %v", err)
	}
	defer router.Stop()

	// 等待几个健康检查周期
	time.Sleep(3 * time.Second)

	// 检查数据中心信息
	dcInfo := router.GetDataCenterInfo()
	if len(dcInfo) == 0 {
		t.Error("未发现任何数据中心")
	}

	healthyDCs := 0
	for dcID, info := range dcInfo {
		if info.IsHealthy {
			healthyDCs++
		}
		
		t.Logf("✓ DC %s: 健康=%t, 节点数=%d, 延迟=%v, 最后检查=%v", 
			dcID, info.IsHealthy, len(info.Nodes), info.Latency, info.LastPing)
	}

	if healthyDCs == 0 {
		t.Error("没有健康的数据中心")
	}

	t.Logf("✓ 健康检查完成: %d个健康DC", healthyDCs)
}

// TestRouterMetrics 测试路由器指标
func TestRouterMetrics(t *testing.T) {
	router := createTestReadWriteRouter()

	// 启动路由器
	err := router.Start()
	if err != nil {
		t.Fatalf("启动读写分离路由器失败: %v", err)
	}
	defer router.Stop()

	// 等待初始化完成
	time.Sleep(100 * time.Millisecond)

	// 执行一些路由操作
	requestCount := 50
	for i := 0; i < requestCount; i++ {
		key := fmt.Sprintf("metrics-test-key-%d", i)
		
		var requestType replication.RequestType
		var consistency replication.ReadConsistencyLevel
		
		if i%3 == 0 {
			requestType = replication.RequestTypeWrite
			consistency = replication.ReadConsistencyStrong
		} else {
			requestType = replication.RequestTypeRead
			if i%2 == 0 {
				consistency = replication.ReadConsistencyEventual
			} else {
				consistency = replication.ReadConsistencyStrong
			}
		}
		
		_, err := router.RouteRequest(requestType, key, consistency)
		if err != nil {
			t.Errorf("指标测试路由失败 (请求%d): %v", i, err)
		}
		
		time.Sleep(10 * time.Millisecond)
	}

	// 等待指标更新
	time.Sleep(1 * time.Second)

	// 获取和验证指标
	metrics := router.GetMetrics()
	if metrics == nil {
		t.Fatal("获取路由器指标失败")
	}

	t.Log("✓ 路由器指标统计:")
	t.Logf("  - 总请求数: %d", metrics.TotalRequests)
	t.Logf("  - 读请求数: %d", metrics.ReadRequests)
	t.Logf("  - 写请求数: %d", metrics.WriteRequests)
	t.Logf("  - 平均路由延迟: %v", metrics.RoutingLatency)
	t.Logf("  - P99延迟: %v", metrics.P99Latency)
	t.Logf("  - 吞吐量: %.2f RPS", metrics.ThroughputRPS)

	if metrics.TotalRequests == 0 {
		t.Error("总请求数为零")
	}

	if metrics.ReadRequests + metrics.WriteRequests != metrics.TotalRequests {
		t.Error("读写请求数统计不一致")
	}

	// 检查DC指标
	for dcID, count := range metrics.DCRequestCounts {
		t.Logf("  - DC %s: 请求数=%d", dcID, count)
	}
}

// 主测试函数
func main() {
	log.Println("开始读写分离路由器测试...")
	
	fmt.Println("=== 读写分离路由器功能测试 ===")
	
	// 创建测试实例
	router := createTestReadWriteRouter()
	if router == nil {
		log.Fatal("创建读写分离路由器失败")
	}
	log.Println("✓ 读写分离路由器创建成功")

	// 启动测试
	err := router.Start()
	if err != nil {
		log.Fatalf("启动失败: %v", err)
	}
	log.Println("✓ 读写分离路由器启动成功")

	// 等待初始化
	time.Sleep(200 * time.Millisecond)

	// 测试读请求路由
	log.Println("\n--- 测试读请求路由 ---")
	readDecision, err := router.RouteRequest(replication.RequestTypeRead, "test-read-key", replication.ReadConsistencyEventual)
	if err != nil {
		log.Fatalf("读请求路由失败: %v", err)
	}
	log.Printf("✓ 读请求路由: 目标节点=%s, 目标DC=%s, 延迟=%v", 
		readDecision.TargetNode, readDecision.TargetDC, readDecision.Latency)

	// 测试写请求路由
	log.Println("\n--- 测试写请求路由 ---")
	writeDecision, err := router.RouteRequest(replication.RequestTypeWrite, "test-write-key", replication.ReadConsistencyStrong)
	if err != nil {
		log.Fatalf("写请求路由失败: %v", err)
	}
	log.Printf("✓ 写请求路由: 目标节点=%s, 目标DC=%s, 延迟=%v", 
		writeDecision.TargetNode, writeDecision.TargetDC, writeDecision.Latency)

	// 测试负载均衡
	log.Println("\n--- 测试负载均衡 ---")
	nodeCount := make(map[raft.NodeID]int)
	for i := 0; i < 10; i++ {
		key := fmt.Sprintf("lb-test-key-%d", i)
		decision, err := router.RouteRequest(replication.RequestTypeRead, key, replication.ReadConsistencyEventual)
		if err != nil {
			log.Printf("负载均衡测试失败 (请求%d): %v", i, err)
			continue
		}
		nodeCount[decision.TargetNode]++
	}

	log.Println("✓ 负载均衡统计:")
	for nodeID, count := range nodeCount {
		log.Printf("  节点 %s: %d 次", nodeID, count)
	}

	// 等待健康检查
	log.Println("\n--- 等待健康检查 ---")
	time.Sleep(2 * time.Second)

	// 检查数据中心信息
	dcInfo := router.GetDataCenterInfo()
	log.Printf("✓ 发现数据中心数量: %d", len(dcInfo))
	for dcID, info := range dcInfo {
		log.Printf("  DC %s: 健康=%t, 节点数=%d, 延迟=%v", 
			dcID, info.IsHealthy, len(info.Nodes), info.Latency)
	}

	// 检查指标
	metrics := router.GetMetrics()
	log.Printf("✓ 路由指标: 总请求=%d, 读请求=%d, 写请求=%d", 
		metrics.TotalRequests, metrics.ReadRequests, metrics.WriteRequests)

	// 停止测试
	err = router.Stop()
	if err != nil {
		log.Fatalf("停止失败: %v", err)
	}
	log.Println("✓ 读写分离路由器停止成功")

	log.Println("\n读写分离路由器测试完成!")
}
