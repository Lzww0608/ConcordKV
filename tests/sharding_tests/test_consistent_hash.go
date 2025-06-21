/*
* @Author: Lzww0608
* @Date: 2025-6-21 22:22:51
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-21 22:22:51
* @Description: ConcordKV consistent hash ring tests
 */

package main

import (
	"fmt"
	"testing"
	"time"

	"raftserver/raft"
	"raftserver/sharding"
)

// MockNodeID 创建模拟节点ID
func mockNodeID(id string) raft.NodeID {
	return raft.NodeID(id)
}

// TestConsistentHashRingBasic 测试一致性哈希环基本功能
func TestConsistentHashRingBasic(t *testing.T) {
	fmt.Println("=== 测试一致性哈希环基本功能 ===")

	// 创建哈希环
	config := sharding.DefaultHashRingConfig()
	ring := sharding.NewConsistentHashRing(config)

	// 添加节点
	nodes := []struct {
		id      raft.NodeID
		address string
		weight  float64
	}{
		{mockNodeID("node1"), "127.0.0.1:8001", 1.0},
		{mockNodeID("node2"), "127.0.0.1:8002", 1.0},
		{mockNodeID("node3"), "127.0.0.1:8003", 1.0},
	}

	for _, node := range nodes {
		err := ring.AddNode(node.id, node.address, node.weight)
		if err != nil {
			t.Fatalf("添加节点失败: %v", err)
		}
		fmt.Printf("✓ 添加节点 %s 成功\n", node.id)
	}

	// 获取统计信息
	stats := ring.GetStats()
	fmt.Printf("总节点数: %d, 总虚拟节点数: %d\n", stats.TotalNodes, stats.TotalVirtualNodes)

	if stats.TotalNodes != 3 {
		t.Errorf("期望节点数为3，实际为%d", stats.TotalNodes)
	}

	expectedVNodes := 3 * config.VirtualNodesPerNode
	if stats.TotalVirtualNodes != expectedVNodes {
		t.Errorf("期望虚拟节点数为%d，实际为%d", expectedVNodes, stats.TotalVirtualNodes)
	}

	fmt.Println("✓ 基本功能测试通过")
}

// TestConsistentHashRingRouting 测试路由功能
func TestConsistentHashRingRouting(t *testing.T) {
	fmt.Println("\n=== 测试路由功能 ===")

	ring := sharding.NewConsistentHashRing(nil)

	// 添加节点
	nodes := []raft.NodeID{
		mockNodeID("node1"),
		mockNodeID("node2"),
		mockNodeID("node3"),
	}

	for i, nodeID := range nodes {
		err := ring.AddNode(nodeID, fmt.Sprintf("127.0.0.1:800%d", i+1), 1.0)
		if err != nil {
			t.Fatalf("添加节点失败: %v", err)
		}
	}

	// 测试键路由
	testKeys := []string{
		"user:1001",
		"user:1002",
		"user:1003",
		"product:2001",
		"product:2002",
		"order:3001",
		"order:3002",
	}

	keyDistribution := make(map[raft.NodeID]int)

	for _, key := range testKeys {
		nodeID, err := ring.GetNode(key)
		if err != nil {
			t.Fatalf("路由键 %s 失败: %v", key, err)
		}

		keyDistribution[nodeID]++
		fmt.Printf("键 %s -> 节点 %s\n", key, nodeID)
	}

	// 检查分布是否合理
	fmt.Println("\n节点分布统计:")
	for nodeID, count := range keyDistribution {
		fmt.Printf("节点 %s: %d 个键\n", nodeID, count)
	}

	// 每个节点都应该有键分配
	if len(keyDistribution) == 0 {
		t.Error("没有键被路由到任何节点")
	}

	fmt.Println("✓ 路由功能测试通过")
}

// TestConsistentHashRingMultiReplicas 测试多副本路由
func TestConsistentHashRingMultiReplicas(t *testing.T) {
	fmt.Println("\n=== 测试多副本路由 ===")

	ring := sharding.NewConsistentHashRing(nil)

	// 添加5个节点
	for i := 1; i <= 5; i++ {
		nodeID := mockNodeID(fmt.Sprintf("node%d", i))
		err := ring.AddNode(nodeID, fmt.Sprintf("127.0.0.1:800%d", i), 1.0)
		if err != nil {
			t.Fatalf("添加节点失败: %v", err)
		}
	}

	// 测试获取多个副本
	testKey := "test:key:123"
	replicaCount := 3

	replicas, err := ring.GetNodes(testKey, replicaCount)
	if err != nil {
		t.Fatalf("获取副本节点失败: %v", err)
	}

	fmt.Printf("键 %s 的 %d 个副本节点:\n", testKey, replicaCount)
	for i, nodeID := range replicas {
		fmt.Printf("  副本 %d: %s\n", i+1, nodeID)
	}

	if len(replicas) != replicaCount {
		t.Errorf("期望副本数为%d，实际为%d", replicaCount, len(replicas))
	}

	// 检查副本节点是否唯一
	nodeSet := make(map[raft.NodeID]bool)
	for _, nodeID := range replicas {
		if nodeSet[nodeID] {
			t.Errorf("发现重复的副本节点: %s", nodeID)
		}
		nodeSet[nodeID] = true
	}

	fmt.Println("✓ 多副本路由测试通过")
}

// TestConsistentHashRingNodeFailure 测试节点故障处理
func TestConsistentHashRingNodeFailure(t *testing.T) {
	fmt.Println("\n=== 测试节点故障处理 ===")

	ring := sharding.NewConsistentHashRing(nil)

	// 添加节点
	nodes := []raft.NodeID{
		mockNodeID("node1"),
		mockNodeID("node2"),
		mockNodeID("node3"),
	}

	for i, nodeID := range nodes {
		err := ring.AddNode(nodeID, fmt.Sprintf("127.0.0.1:800%d", i+1), 1.0)
		if err != nil {
			t.Fatalf("添加节点失败: %v", err)
		}
	}

	testKey := "test:key:failure"

	// 获取初始路由
	originalNode, err := ring.GetNode(testKey)
	if err != nil {
		t.Fatalf("获取初始路由失败: %v", err)
	}
	fmt.Printf("原始路由: %s -> %s\n", testKey, originalNode)

	// 移除一个节点
	removedNode := nodes[1]
	err = ring.RemoveNode(removedNode)
	if err != nil {
		t.Fatalf("移除节点失败: %v", err)
	}
	fmt.Printf("移除节点: %s\n", removedNode)

	// 获取新路由
	newNode, err := ring.GetNode(testKey)
	if err != nil {
		t.Fatalf("获取新路由失败: %v", err)
	}
	fmt.Printf("新路由: %s -> %s\n", testKey, newNode)

	// 检查统计信息
	stats := ring.GetStats()
	if stats.TotalNodes != 2 {
		t.Errorf("期望节点数为2，实际为%d", stats.TotalNodes)
	}

	fmt.Println("✓ 节点故障处理测试通过")
}

// TestConsistentHashRingLoadBalance 测试负载均衡
func TestConsistentHashRingLoadBalance(t *testing.T) {
	fmt.Println("\n=== 测试负载均衡 ===")

	ring := sharding.NewConsistentHashRing(nil)

	// 添加不同权重的节点
	nodeConfigs := []struct {
		id     raft.NodeID
		weight float64
	}{
		{mockNodeID("node1"), 1.0},
		{mockNodeID("node2"), 2.0}, // 权重更高
		{mockNodeID("node3"), 1.0},
	}

	for i, config := range nodeConfigs {
		err := ring.AddNode(config.id, fmt.Sprintf("127.0.0.1:800%d", i+1), config.weight)
		if err != nil {
			t.Fatalf("添加节点失败: %v", err)
		}
		fmt.Printf("添加节点 %s，权重: %.1f\n", config.id, config.weight)
	}

	// 模拟负载指标
	loadMetrics := map[raft.NodeID]*sharding.LoadMetrics{
		mockNodeID("node1"): {QPS: 100, StorageUsage: 50, CPUUsage: 0.3, MemoryUsage: 0.4, LastUpdate: time.Now().Unix()},
		mockNodeID("node2"): {QPS: 300, StorageUsage: 80, CPUUsage: 0.8, MemoryUsage: 0.7, LastUpdate: time.Now().Unix()}, // 高负载
		mockNodeID("node3"): {QPS: 120, StorageUsage: 45, CPUUsage: 0.4, MemoryUsage: 0.3, LastUpdate: time.Now().Unix()},
	}

	// 更新负载指标
	for nodeID, metrics := range loadMetrics {
		err := ring.UpdateLoadMetrics(nodeID, metrics)
		if err != nil {
			t.Fatalf("更新负载指标失败: %v", err)
		}
		fmt.Printf("节点 %s: QPS=%.0f, CPU=%.1f%%, 内存=%.1f%%\n",
			nodeID, metrics.QPS, metrics.CPUUsage*100, metrics.MemoryUsage*100)
	}

	// 检查负载不平衡
	imbalance := ring.GetLoadImbalance()
	fmt.Println("\n负载不平衡检测:")
	for nodeID, deviation := range imbalance {
		fmt.Printf("节点 %s: 负载偏差 %.2f%%\n", nodeID, deviation*100)
	}

	// node2应该被检测为高负载
	if len(imbalance) == 0 {
		t.Error("应该检测到负载不平衡")
	}

	fmt.Println("✓ 负载均衡测试通过")
}

// TestConsistentHashRingConsistency 测试一致性
func TestConsistentHashRingConsistency(t *testing.T) {
	fmt.Println("\n=== 测试一致性 ===")

	ring := sharding.NewConsistentHashRing(nil)

	// 添加初始节点
	initialNodes := []raft.NodeID{
		mockNodeID("node1"),
		mockNodeID("node2"),
		mockNodeID("node3"),
	}

	for i, nodeID := range initialNodes {
		err := ring.AddNode(nodeID, fmt.Sprintf("127.0.0.1:800%d", i+1), 1.0)
		if err != nil {
			t.Fatalf("添加节点失败: %v", err)
		}
	}

	// 生成测试键并记录初始路由
	numKeys := 1000
	initialRouting := make(map[string]raft.NodeID)

	for i := 0; i < numKeys; i++ {
		key := fmt.Sprintf("key:%d", i)
		nodeID, err := ring.GetNode(key)
		if err != nil {
			t.Fatalf("路由键失败: %v", err)
		}
		initialRouting[key] = nodeID
	}

	// 添加新节点
	newNode := mockNodeID("node4")
	err := ring.AddNode(newNode, "127.0.0.1:8004", 1.0)
	if err != nil {
		t.Fatalf("添加新节点失败: %v", err)
	}

	// 检查路由变化
	changedKeys := 0
	for key, originalNode := range initialRouting {
		newNodeID, err := ring.GetNode(key)
		if err != nil {
			t.Fatalf("重新路由键失败: %v", err)
		}

		if newNodeID != originalNode {
			changedKeys++
		}
	}

	// 计算变化比例
	changeRatio := float64(changedKeys) / float64(numKeys)
	fmt.Printf("添加节点后，%d/%d (%.2f%%) 的键路由发生变化\n",
		changedKeys, numKeys, changeRatio*100)

	// 一致性哈希应该最小化路由变化
	// 理论上应该只有 1/n 的键发生变化，其中 n 是节点总数
	expectedChangeRatio := 1.0 / float64(len(initialNodes)+1)
	tolerance := 0.1 // 10% 容差

	if changeRatio > expectedChangeRatio+tolerance {
		t.Errorf("路由变化比例过高: %.2f%%, 期望约 %.2f%%",
			changeRatio*100, expectedChangeRatio*100)
	}

	fmt.Println("✓ 一致性测试通过")
}

// BenchmarkConsistentHashRingRouting 路由性能基准测试
func BenchmarkConsistentHashRingRouting(b *testing.B) {
	ring := sharding.NewConsistentHashRing(nil)

	// 添加节点
	for i := 1; i <= 10; i++ {
		nodeID := mockNodeID(fmt.Sprintf("node%d", i))
		ring.AddNode(nodeID, fmt.Sprintf("127.0.0.1:800%d", i), 1.0)
	}

	// 准备测试键
	keys := make([]string, 1000)
	for i := 0; i < 1000; i++ {
		keys[i] = fmt.Sprintf("benchmark:key:%d", i)
	}

	b.ResetTimer()
	b.ReportAllocs()

	for i := 0; i < b.N; i++ {
		key := keys[i%len(keys)]
		_, err := ring.GetNode(key)
		if err != nil {
			b.Fatalf("路由失败: %v", err)
		}
	}
}

// main 函数运行所有测试
func main() {
	fmt.Println("开始运行一致性哈希环测试...")

	// 创建测试实例
	t := &testing.T{}

	// 运行所有测试
	TestConsistentHashRingBasic(t)
	TestConsistentHashRingRouting(t)
	TestConsistentHashRingMultiReplicas(t)
	TestConsistentHashRingNodeFailure(t)
	TestConsistentHashRingLoadBalance(t)
	TestConsistentHashRingConsistency(t)

	if t.Failed() {
		fmt.Println("\n❌ 部分测试失败")
		return
	}

	fmt.Println("\n✅ 所有测试通过！")

	// 运行性能基准测试
	fmt.Println("\n开始性能基准测试...")
	result := testing.Benchmark(BenchmarkConsistentHashRingRouting)
	fmt.Printf("性能测试结果: %s\n", result.String())
}
