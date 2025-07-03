/*
* @Author: Lzww0608
* @Date: 2025-7-2 22:27:21
* @LastEditors: Lzww0608
* @LastEditTime: 2025-7-2 22:27:24
* @Description: ConcordKV intelligent client - smart routing tests
 */

package concord

import (
	"context"
	"fmt"
	"sync"
	"testing"
	"time"
)

// TestSmartRouter_BasicRouting 测试基本路由功能
func TestSmartRouter_BasicRouting(t *testing.T) {
	config := DefaultSmartRouterConfig()
	topologyCache := NewTopologyCache(DefaultTopologyConfig())
	router := NewSmartRouter(config, topologyCache)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := router.Start(ctx)
	if err != nil {
		t.Fatalf("启动智能路由器失败: %v", err)
	}
	defer router.Stop()

	// 准备测试数据
	shardInfo := &ShardInfo{
		ID:       "shard-001",
		Range:    ShardRange{StartHash: 0, EndHash: 1000},
		Primary:  NodeID("node-1"),
		Replicas: []NodeID{NodeID("node-2"), NodeID("node-3")},
		State:    ShardStateActive,
		Version:  1,
	}

	// 设置分片信息到拓扑缓存
	topologyCache.Set(shardInfo)
	topologyCache.SetKeyMapping("test-key", shardInfo.ID)

	// 测试写请求路由到主节点
	writeRequest := &RoutingRequest{
		Key:      "test-key",
		Strategy: RoutingWritePrimary,
		ReadOnly: false,
		Context:  ctx,
	}

	result, err := router.Route(writeRequest)
	if err != nil {
		t.Fatalf("路由写请求失败: %v", err)
	}

	if result.TargetNode != shardInfo.Primary {
		t.Errorf("写请求应该路由到主节点，期望: %s, 实际: %s", shardInfo.Primary, result.TargetNode)
	}

	if result.Strategy != RoutingWritePrimary {
		t.Errorf("路由策略不正确，期望: %s, 实际: %s", RoutingWritePrimary, result.Strategy)
	}

	// 测试读请求路由到副本节点
	readRequest := &RoutingRequest{
		Key:      "test-key",
		Strategy: RoutingReadReplica,
		ReadOnly: true,
		Context:  ctx,
	}

	result, err = router.Route(readRequest)
	if err != nil {
		t.Fatalf("路由读请求失败: %v", err)
	}

	// 验证目标节点是副本节点之一或主节点（如果没有副本）
	isValidTarget := result.TargetNode == shardInfo.Primary
	for _, replica := range shardInfo.Replicas {
		if result.TargetNode == replica {
			isValidTarget = true
			break
		}
	}

	if !isValidTarget {
		t.Errorf("读请求路由到了无效的节点: %s", result.TargetNode)
	}
}

// TestSmartRouter_LoadBalancing 测试负载均衡
func TestSmartRouter_LoadBalancing(t *testing.T) {
	config := DefaultSmartRouterConfig()
	config.LoadBalanceAlgorithm = LBRoundRobin

	topologyCache := NewTopologyCache(DefaultTopologyConfig())
	router := NewSmartRouter(config, topologyCache)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := router.Start(ctx)
	if err != nil {
		t.Fatalf("启动智能路由器失败: %v", err)
	}
	defer router.Stop()

	// 准备多个副本的分片
	shardInfo := &ShardInfo{
		ID:       "shard-001",
		Range:    ShardRange{StartHash: 0, EndHash: 1000},
		Primary:  NodeID("node-1"),
		Replicas: []NodeID{NodeID("node-2"), NodeID("node-3"), NodeID("node-4")},
		State:    ShardStateActive,
		Version:  1,
	}

	topologyCache.Set(shardInfo)
	topologyCache.SetKeyMapping("test-key", shardInfo.ID)

	// 执行多次负载均衡路由
	targetNodes := make(map[NodeID]int)
	routeCount := 100

	for i := 0; i < routeCount; i++ {
		request := &RoutingRequest{
			Key:      "test-key",
			Strategy: RoutingLoadBalance,
			ReadOnly: true,
			Context:  ctx,
		}

		result, err := router.Route(request)
		if err != nil {
			t.Fatalf("第%d次负载均衡路由失败: %v", i, err)
		}

		targetNodes[result.TargetNode]++
	}

	// 验证负载分布
	if len(targetNodes) == 0 {
		t.Fatal("没有路由到任何节点")
	}

	// 验证所有目标节点都是有效的
	allNodes := append([]NodeID{shardInfo.Primary}, shardInfo.Replicas...)
	for targetNode := range targetNodes {
		isValid := false
		for _, validNode := range allNodes {
			if targetNode == validNode {
				isValid = true
				break
			}
		}
		if !isValid {
			t.Errorf("路由到了无效节点: %s", targetNode)
		}
	}

	t.Logf("负载分布: %v", targetNodes)
}

// TestSmartRouter_FailoverStrategy 测试故障转移策略
func TestSmartRouter_FailoverStrategy(t *testing.T) {
	config := DefaultSmartRouterConfig()
	config.FailureThreshold = 1 // 设置失败阈值为1，便于测试
	topologyCache := NewTopologyCache(DefaultTopologyConfig())
	router := NewSmartRouter(config, topologyCache)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := router.Start(ctx)
	if err != nil {
		t.Fatalf("启动智能路由器失败: %v", err)
	}
	defer router.Stop()

	shardInfo := &ShardInfo{
		ID:       "shard-001",
		Range:    ShardRange{StartHash: 0, EndHash: 1000},
		Primary:  NodeID("node-1"),
		Replicas: []NodeID{NodeID("node-2"), NodeID("node-3")},
		State:    ShardStateActive,
		Version:  1,
	}

	topologyCache.Set(shardInfo)
	topologyCache.SetKeyMapping("test-key", shardInfo.ID)

	// 模拟主节点故障 - 确保达到失败阈值
	router.UpdateNodeHealth(shardInfo.Primary, false, 0, fmt.Errorf("节点故障"))

	// 测试故障转移路由
	request := &RoutingRequest{
		Key:      "test-key",
		Strategy: RoutingFailover,
		ReadOnly: false,
		Context:  ctx,
	}

	result, err := router.Route(request)
	if err != nil {
		t.Fatalf("故障转移路由失败: %v", err)
	}

	// 应该路由到副本节点
	isRoutedToReplica := false
	for _, replica := range shardInfo.Replicas {
		if result.TargetNode == replica {
			isRoutedToReplica = true
			break
		}
	}

	if !isRoutedToReplica {
		t.Errorf("故障转移应该路由到副本节点，实际路由到: %s", result.TargetNode)
	}
}

// TestSmartRouter_CircuitBreaker 测试熔断器功能
func TestSmartRouter_CircuitBreaker(t *testing.T) {
	config := DefaultSmartRouterConfig()
	config.CircuitBreakerEnabled = true
	config.FailureRateThreshold = 0.5
	config.MinRequestThreshold = 5

	// 创建熔断器
	circuitBreaker := NewCircuitBreaker(config)

	// 测试正常调用
	err := circuitBreaker.Call(func() error {
		return nil // 成功
	})
	if err != nil {
		t.Errorf("正常调用不应该失败: %v", err)
	}

	// 模拟多次失败调用
	for i := 0; i < 10; i++ {
		circuitBreaker.Call(func() error {
			return fmt.Errorf("模拟错误")
		})
	}

	// 验证熔断器状态
	state := circuitBreaker.GetState()
	if state != CircuitOpen {
		t.Errorf("熔断器应该处于开启状态，实际状态: %s", state)
	}

	// 测试熔断器开启时的调用
	err = circuitBreaker.Call(func() error {
		return nil
	})
	if err == nil {
		t.Error("熔断器开启时调用应该失败")
	}
}

// TestSmartRouter_ConsistentHashRing 测试一致性哈希环
func TestSmartRouter_ConsistentHashRing(t *testing.T) {
	hashRing := NewConsistentHashRing(100) // 100个虚拟节点

	// 添加节点
	nodes := []NodeID{"node-1", "node-2", "node-3", "node-4"}
	for _, node := range nodes {
		hashRing.AddNode(node)
	}

	// 测试键的路由一致性
	key := "test-key-123"
	node1, err := hashRing.GetNode(key)
	if err != nil {
		t.Fatalf("获取节点失败: %v", err)
	}

	// 多次获取同一个键应该返回相同的节点
	for i := 0; i < 10; i++ {
		node, err := hashRing.GetNode(key)
		if err != nil {
			t.Fatalf("第%d次获取节点失败: %v", i, err)
		}
		if node != node1 {
			t.Errorf("第%d次获取的节点不一致，期望: %s, 实际: %s", i, node1, node)
		}
	}

	// 测试负载分布
	keyDistribution := make(map[NodeID]int)
	for i := 0; i < 1000; i++ {
		key := fmt.Sprintf("test-key-%d", i)
		node, err := hashRing.GetNode(key)
		if err != nil {
			t.Fatalf("获取键 %s 的节点失败: %v", key, err)
		}
		keyDistribution[node]++
	}

	// 验证所有节点都有键分配
	for _, node := range nodes {
		if keyDistribution[node] == 0 {
			t.Errorf("节点 %s 没有分配到任何键", node)
		}
	}

	t.Logf("键分布: %v", keyDistribution)

	// 测试节点移除
	hashRing.RemoveNode("node-1")

	// 验证移除节点后的路由
	newNode, err := hashRing.GetNode(key)
	if err != nil {
		t.Fatalf("移除节点后获取节点失败: %v", err)
	}
	if newNode == "node-1" {
		t.Error("不应该路由到已移除的节点")
	}
}

// TestSmartRouter_BatchRouting 测试批量路由
func TestSmartRouter_BatchRouting(t *testing.T) {
	config := DefaultSmartRouterConfig()
	topologyCache := NewTopologyCache(DefaultTopologyConfig())
	router := NewSmartRouter(config, topologyCache)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err := router.Start(ctx)
	if err != nil {
		t.Fatalf("启动智能路由器失败: %v", err)
	}
	defer router.Stop()

	// 准备多个分片
	shards := []*ShardInfo{
		{
			ID:       "shard-001",
			Range:    ShardRange{StartHash: 0, EndHash: 1000},
			Primary:  NodeID("node-1"),
			Replicas: []NodeID{NodeID("node-2")},
			State:    ShardStateActive,
			Version:  1,
		},
		{
			ID:       "shard-002",
			Range:    ShardRange{StartHash: 1000, EndHash: 2000},
			Primary:  NodeID("node-3"),
			Replicas: []NodeID{NodeID("node-4")},
			State:    ShardStateActive,
			Version:  1,
		},
	}

	// 设置分片信息
	for i, shard := range shards {
		topologyCache.Set(shard)
		topologyCache.SetKeyMapping(fmt.Sprintf("key-%d", i), shard.ID)
	}

	// 准备批量路由请求
	requests := []*RoutingRequest{
		{
			Key:      "key-0",
			Strategy: RoutingWritePrimary,
			ReadOnly: false,
			Context:  ctx,
		},
		{
			Key:      "key-1",
			Strategy: RoutingReadReplica,
			ReadOnly: true,
			Context:  ctx,
		},
	}

	// 执行批量路由
	results, err := router.RouteBatch(requests)
	if err != nil {
		t.Fatalf("批量路由失败: %v", err)
	}

	// 验证结果
	if len(results) != len(requests) {
		t.Fatalf("结果数量不匹配，期望: %d, 实际: %d", len(requests), len(results))
	}

	// 验证第一个请求路由到 shard-001 的主节点
	result0, exists := results["key-0"]
	if !exists {
		t.Fatal("缺少 key-0 的路由结果")
	}
	if result0.TargetNode != shards[0].Primary {
		t.Errorf("key-0 应该路由到 %s，实际: %s", shards[0].Primary, result0.TargetNode)
	}

	// 验证第二个请求路由到 shard-002 的副本或主节点
	result1, exists := results["key-1"]
	if !exists {
		t.Fatal("缺少 key-1 的路由结果")
	}

	validTargets := append([]NodeID{shards[1].Primary}, shards[1].Replicas...)
	isValidTarget := false
	for _, target := range validTargets {
		if result1.TargetNode == target {
			isValidTarget = true
			break
		}
	}
	if !isValidTarget {
		t.Errorf("key-1 路由到了无效目标: %s", result1.TargetNode)
	}
}

// TestSmartRouter_CacheOperations 测试路由缓存
func TestSmartRouter_CacheOperations(t *testing.T) {
	config := DefaultSmartRouterConfig()
	config.EnableCache = true
	config.CacheSize = 100
	config.CacheTTL = 1 * time.Minute

	topologyCache := NewTopologyCache(DefaultTopologyConfig())
	router := NewSmartRouter(config, topologyCache)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := router.Start(ctx)
	if err != nil {
		t.Fatalf("启动智能路由器失败: %v", err)
	}
	defer router.Stop()

	shardInfo := &ShardInfo{
		ID:      "shard-001",
		Primary: NodeID("node-1"),
		State:   ShardStateActive,
		Version: 1,
	}

	topologyCache.Set(shardInfo)
	topologyCache.SetKeyMapping("test-key", shardInfo.ID)

	request := &RoutingRequest{
		Key:      "test-key",
		Strategy: RoutingWritePrimary,
		ReadOnly: false,
		Context:  ctx,
	}

	// 第一次路由（缓存未命中）
	result1, err := router.Route(request)
	if err != nil {
		t.Fatalf("第一次路由失败: %v", err)
	}
	if result1.Cached {
		t.Error("第一次路由不应该来自缓存")
	}

	// 第二次路由（应该命中缓存）
	result2, err := router.Route(request)
	if err != nil {
		t.Fatalf("第二次路由失败: %v", err)
	}
	if !result2.Cached {
		t.Error("第二次路由应该来自缓存")
	}

	// 验证统计信息
	stats := router.GetStats()
	if stats.CacheHits == 0 {
		t.Error("应该有缓存命中统计")
	}
	if stats.CacheMisses == 0 {
		t.Error("应该有缓存未命中统计")
	}
}

// TestSmartRouter_NodeHealthManagement 测试节点健康管理
func TestSmartRouter_NodeHealthManagement(t *testing.T) {
	config := DefaultSmartRouterConfig()
	config.FailureThreshold = 3
	config.RecoveryThreshold = 2

	topologyCache := NewTopologyCache(DefaultTopologyConfig())
	router := NewSmartRouter(config, topologyCache)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := router.Start(ctx)
	if err != nil {
		t.Fatalf("启动智能路由器失败: %v", err)
	}
	defer router.Stop()

	nodeID := NodeID("node-1")

	// 初始状态应该是健康的
	stats := router.GetStats()
	if nodeStats, exists := stats.NodeStats[nodeID]; exists {
		if nodeStats.Status != NodeHealthy {
			t.Errorf("初始节点状态应该是健康的，实际: %s", nodeStats.Status)
		}
	}

	// 报告多次失败
	for i := 0; i < 4; i++ {
		router.UpdateNodeHealth(nodeID, false, 0, fmt.Errorf("连接失败"))
	}

	// 节点应该变为不健康
	stats = router.GetStats()
	if nodeStats, exists := stats.NodeStats[nodeID]; exists {
		if nodeStats.Status != NodeUnhealthy {
			t.Errorf("节点应该变为不健康，实际状态: %s", nodeStats.Status)
		}
	}

	// 报告成功，但不足以恢复
	router.UpdateNodeHealth(nodeID, true, 100*time.Millisecond, nil)
	stats = router.GetStats()
	if nodeStats, exists := stats.NodeStats[nodeID]; exists {
		if nodeStats.Status != NodeRecovering {
			t.Errorf("节点应该处于恢复状态，实际状态: %s", nodeStats.Status)
		}
	}

	// 继续报告成功直到完全恢复
	for i := 0; i < 2; i++ {
		router.UpdateNodeHealth(nodeID, true, 50*time.Millisecond, nil)
	}

	stats = router.GetStats()
	if nodeStats, exists := stats.NodeStats[nodeID]; exists {
		if nodeStats.Status != NodeHealthy {
			t.Errorf("节点应该恢复健康，实际状态: %s", nodeStats.Status)
		}
	}
}

// TestRoundRobinLoadBalancer 测试轮询负载均衡器
func TestRoundRobinLoadBalancer(t *testing.T) {
	lb := NewRoundRobinLoadBalancer()

	nodes := []NodeID{"node-1", "node-2", "node-3"}

	// 测试轮询分配
	results := make([]NodeID, 9)
	for i := 0; i < 9; i++ {
		node, err := lb.Select(nodes, fmt.Sprintf("key-%d", i))
		if err != nil {
			t.Fatalf("第%d次选择失败: %v", i, err)
		}
		results[i] = node
	}

	// 验证轮询模式：每3次应该循环一次
	expected := []NodeID{
		"node-2", "node-3", "node-1", // 第一轮（从索引1开始，因为counter初始为0，加1后为1）
		"node-2", "node-3", "node-1", // 第二轮
		"node-2", "node-3", "node-1", // 第三轮
	}

	for i, result := range results {
		if result != expected[i] {
			t.Errorf("第%d次选择结果不正确，期望: %s, 实际: %s", i, expected[i], result)
		}
	}

	// 测试统计信息
	stats := lb.GetStats()
	for _, node := range nodes {
		if stats[node] != 3 {
			t.Errorf("节点 %s 的统计不正确，期望: 3, 实际: %d", node, stats[node])
		}
	}
}

// BenchmarkSmartRouter_Route 性能测试：单次路由
func BenchmarkSmartRouter_Route(b *testing.B) {
	config := DefaultSmartRouterConfig()
	topologyCache := NewTopologyCache(DefaultTopologyConfig())
	router := NewSmartRouter(config, topologyCache)

	ctx := context.Background()
	router.Start(ctx)
	defer router.Stop()

	// 准备测试数据
	shardInfo := &ShardInfo{
		ID:      "shard-001",
		Primary: NodeID("node-1"),
		State:   ShardStateActive,
		Version: 1,
	}
	topologyCache.Set(shardInfo)
	topologyCache.SetKeyMapping("test-key", shardInfo.ID)

	request := &RoutingRequest{
		Key:      "test-key",
		Strategy: RoutingWritePrimary,
		ReadOnly: false,
		Context:  ctx,
	}

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			_, err := router.Route(request)
			if err != nil {
				b.Fatalf("路由失败: %v", err)
			}
		}
	})
}

// BenchmarkSmartRouter_RouteBatch 性能测试：批量路由
func BenchmarkSmartRouter_RouteBatch(b *testing.B) {
	config := DefaultSmartRouterConfig()
	topologyCache := NewTopologyCache(DefaultTopologyConfig())
	router := NewSmartRouter(config, topologyCache)

	ctx := context.Background()
	router.Start(ctx)
	defer router.Stop()

	// 准备测试数据
	batchSize := 100
	requests := make([]*RoutingRequest, batchSize)

	for i := 0; i < batchSize; i++ {
		shardInfo := &ShardInfo{
			ID:      fmt.Sprintf("shard-%03d", i%10),
			Primary: NodeID(fmt.Sprintf("node-%d", i%5)),
			State:   ShardStateActive,
			Version: 1,
		}
		topologyCache.Set(shardInfo)

		key := fmt.Sprintf("key-%d", i)
		topologyCache.SetKeyMapping(key, shardInfo.ID)

		requests[i] = &RoutingRequest{
			Key:      key,
			Strategy: RoutingWritePrimary,
			ReadOnly: false,
			Context:  ctx,
		}
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, err := router.RouteBatch(requests)
		if err != nil {
			b.Fatalf("批量路由失败: %v", err)
		}
	}
}

// BenchmarkConsistentHashRing_GetNode 性能测试：一致性哈希
func BenchmarkConsistentHashRing_GetNode(b *testing.B) {
	hashRing := NewConsistentHashRing(100)

	// 添加节点
	for i := 0; i < 10; i++ {
		hashRing.AddNode(NodeID(fmt.Sprintf("node-%d", i)))
	}

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			key := fmt.Sprintf("key-%d", i)
			_, err := hashRing.GetNode(key)
			if err != nil {
				b.Fatalf("获取节点失败: %v", err)
			}
			i++
		}
	})
}

// TestSmartRouter_ConcurrentAccess 并发访问测试
func TestSmartRouter_ConcurrentAccess(t *testing.T) {
	config := DefaultSmartRouterConfig()
	topologyCache := NewTopologyCache(DefaultTopologyConfig())
	router := NewSmartRouter(config, topologyCache)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err := router.Start(ctx)
	if err != nil {
		t.Fatalf("启动智能路由器失败: %v", err)
	}
	defer router.Stop()

	// 准备测试数据
	shardInfo := &ShardInfo{
		ID:      "shard-001",
		Primary: NodeID("node-1"),
		State:   ShardStateActive,
		Version: 1,
	}
	topologyCache.Set(shardInfo)
	topologyCache.SetKeyMapping("test-key", shardInfo.ID)

	// 并发测试
	concurrency := 50
	requestsPerGoroutine := 100
	var wg sync.WaitGroup
	var errors []error
	var errorMu sync.Mutex

	for i := 0; i < concurrency; i++ {
		wg.Add(1)
		go func(goroutineID int) {
			defer wg.Done()

			for j := 0; j < requestsPerGoroutine; j++ {
				request := &RoutingRequest{
					Key:      "test-key",
					Strategy: RoutingWritePrimary,
					ReadOnly: false,
					Context:  ctx,
				}

				_, err := router.Route(request)
				if err != nil {
					errorMu.Lock()
					errors = append(errors, fmt.Errorf("goroutine %d, request %d: %w", goroutineID, j, err))
					errorMu.Unlock()
				}
			}
		}(i)
	}

	wg.Wait()

	if len(errors) > 0 {
		t.Fatalf("并发测试发现 %d 个错误，第一个错误: %v", len(errors), errors[0])
	}

	// 验证统计信息
	stats := router.GetStats()
	expectedRequests := int64(concurrency * requestsPerGoroutine)
	if stats.TotalRequests < expectedRequests {
		t.Errorf("总请求数不正确，期望至少: %d, 实际: %d", expectedRequests, stats.TotalRequests)
	}

	t.Logf("并发测试完成：%d个goroutine，每个%d次请求，总计%d次请求", concurrency, requestsPerGoroutine, stats.TotalRequests)
}
