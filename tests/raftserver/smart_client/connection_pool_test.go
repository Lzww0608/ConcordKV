/*
* @Author: Lzww0608
* @Date: 2025-7-2 22:27:34
* @LastEditors: Lzww0608
* @LastEditTime: 2025-7-2 22:27:37
* @Description: ConcordKV intelligent client - enhanced connection pool tests
 */

package concord

import (
	"context"
	"fmt"
	"sync"
	"testing"
	"time"
)

// TestConnection_BasicOperations 测试连接基本操作
func TestConnection_BasicOperations(t *testing.T) {
	nodeID := NodeID("node-1")
	shardID := "shard-001"
	address := "localhost:8080"
	timeout := 5 * time.Second

	conn := NewConnection("test-conn-1", nodeID, shardID, address, timeout)

	// 测试初始状态
	if !conn.IsIdle() {
		t.Error("新创建的连接应该是空闲状态")
	}

	// 测试标记为使用
	conn.MarkUsed()
	if conn.IsIdle() {
		t.Error("标记为使用后连接不应该是空闲状态")
	}

	// 测试标记为空闲
	conn.MarkIdle()
	if !conn.IsIdle() {
		t.Error("标记为空闲后连接应该是空闲状态")
	}

	// 测试健康检查
	if !conn.IsHealthy() {
		t.Error("新连接应该是健康的")
	}

	// 测试统计信息
	stats := conn.GetStats()
	if stats.ID != "test-conn-1" {
		t.Errorf("连接ID不正确，期望: test-conn-1, 实际: %s", stats.ID)
	}
	if stats.NodeID != nodeID {
		t.Errorf("节点ID不正确，期望: %s, 实际: %s", nodeID, stats.NodeID)
	}
	if stats.ShardID != shardID {
		t.Errorf("分片ID不正确，期望: %s, 实际: %s", shardID, stats.ShardID)
	}

	// 测试关闭连接
	err := conn.Close()
	if err != nil {
		t.Errorf("关闭连接失败: %v", err)
	}
}

// TestConnectionPool_BasicOperations 测试连接池基本操作
func TestConnectionPool_BasicOperations(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 2
	config.MaxConnections = 10
	config.InitialSize = 3
	config.EnablePreWarm = false // 禁用预热以便测试

	nodeID := NodeID("node-1")
	shardID := "shard-001"
	address := "localhost:8080"
	factory := NewMockConnectionFactory(5 * time.Second)

	pool := NewConnectionPool(config, nodeID, shardID, address, factory)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	// 启动连接池
	err := pool.Start(ctx)
	if err != nil {
		t.Fatalf("启动连接池失败: %v", err)
	}
	defer pool.Stop()

	// 测试获取连接
	conn, err := pool.Get(ctx)
	if err != nil {
		t.Fatalf("获取连接失败: %v", err)
	}
	if conn == nil {
		t.Fatal("获取的连接不应该为空")
	}

	// 测试归还连接
	pool.Put(conn)

	// 测试统计信息
	stats := pool.GetStats()
	if stats.NodeID != nodeID {
		t.Errorf("统计信息中的节点ID不正确，期望: %s, 实际: %s", nodeID, stats.NodeID)
	}
	if stats.TotalConnections < int64(config.InitialSize) {
		t.Errorf("总连接数不正确，期望至少: %d, 实际: %d", config.InitialSize, stats.TotalConnections)
	}
}

// TestConnectionPool_MaxConnections 测试最大连接数限制
func TestConnectionPool_MaxConnections(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 1
	config.MaxConnections = 3
	config.InitialSize = 1
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	pool := NewConnectionPool(config, "node-1", "shard-001", "localhost:8080", factory)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := pool.Start(ctx)
	if err != nil {
		t.Fatalf("启动连接池失败: %v", err)
	}
	defer pool.Stop()

	// 获取所有可用连接
	connections := make([]*Connection, 0, config.MaxConnections)
	for i := 0; i < config.MaxConnections; i++ {
		conn, err := pool.Get(ctx)
		if err != nil {
			t.Fatalf("获取第%d个连接失败: %v", i+1, err)
		}
		connections = append(connections, conn)
	}

	// 验证已达到最大连接数
	stats := pool.GetStats()
	if stats.TotalConnections != int64(config.MaxConnections) {
		t.Errorf("总连接数应该等于最大连接数，期望: %d, 实际: %d", config.MaxConnections, stats.TotalConnections)
	}

	// 尝试获取超出限制的连接（应该超时）
	shortCtx, shortCancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer shortCancel()

	_, err = pool.Get(shortCtx)
	if err == nil {
		t.Error("获取超出最大连接数限制的连接应该失败")
	}

	// 归还连接
	for _, conn := range connections {
		pool.Put(conn)
	}
}

// TestConnectionPool_AutoScale 测试自动扩缩容
func TestConnectionPool_AutoScale(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 2
	config.MaxConnections = 10
	config.InitialSize = 2
	config.EnableAutoScale = true
	config.ScaleUpThreshold = 0.8
	config.ScaleDownThreshold = 0.2
	config.ScaleUpStep = 2
	config.ScaleDownStep = 1
	config.ScaleInterval = 100 * time.Millisecond
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	pool := NewConnectionPool(config, "node-1", "shard-001", "localhost:8080", factory)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err := pool.Start(ctx)
	if err != nil {
		t.Fatalf("启动连接池失败: %v", err)
	}
	defer pool.Stop()

	// 等待初始化完成
	time.Sleep(200 * time.Millisecond)

	initialStats := pool.GetStats()
	t.Logf("初始连接数: %d", initialStats.TotalConnections)

	// 获取大部分连接以触发扩容
	connections := make([]*Connection, 0)
	for i := 0; i < 2; i++ { // 获取所有初始连接
		conn, err := pool.Get(ctx)
		if err != nil {
			t.Fatalf("获取连接失败: %v", err)
		}
		connections = append(connections, conn)
	}

	// 等待自动扩容
	time.Sleep(500 * time.Millisecond)

	scaleUpStats := pool.GetStats()
	t.Logf("扩容后连接数: %d", scaleUpStats.TotalConnections)

	// 归还所有连接
	for _, conn := range connections {
		pool.Put(conn)
	}

	// 等待自动缩容
	time.Sleep(500 * time.Millisecond)

	scaleDownStats := pool.GetStats()
	t.Logf("缩容后连接数: %d", scaleDownStats.TotalConnections)

	// 验证缩容效果（应该不低于最小连接数）
	if scaleDownStats.TotalConnections < int64(config.MinConnections) {
		t.Errorf("缩容后连接数不应该低于最小连接数，期望至少: %d, 实际: %d", config.MinConnections, scaleDownStats.TotalConnections)
	}
}

// TestConnectionPool_PreWarm 测试连接预热
func TestConnectionPool_PreWarm(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 1
	config.MaxConnections = 10
	config.InitialSize = 1
	config.EnablePreWarm = true
	config.PreWarmSize = 5
	config.PreWarmConcurrency = 3

	factory := NewMockConnectionFactory(5 * time.Second)
	pool := NewConnectionPool(config, "node-1", "shard-001", "localhost:8080", factory)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err := pool.Start(ctx)
	if err != nil {
		t.Fatalf("启动连接池失败: %v", err)
	}
	defer pool.Stop()

	// 等待预热完成
	time.Sleep(1 * time.Second)

	stats := pool.GetStats()
	expectedTotal := int64(config.InitialSize + config.PreWarmSize)
	if stats.TotalConnections < expectedTotal {
		t.Errorf("预热后连接数不正确，期望至少: %d, 实际: %d", expectedTotal, stats.TotalConnections)
	}

	t.Logf("预热后总连接数: %d", stats.TotalConnections)
}

// TestConnectionPool_HealthCheck 测试健康检查
func TestConnectionPool_HealthCheck(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 2
	config.MaxConnections = 5
	config.InitialSize = 2
	config.HealthCheckInterval = 100 * time.Millisecond
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	pool := NewConnectionPool(config, "node-1", "shard-001", "localhost:8080", factory)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := pool.Start(ctx)
	if err != nil {
		t.Fatalf("启动连接池失败: %v", err)
	}
	defer pool.Stop()

	// 等待健康检查运行
	time.Sleep(300 * time.Millisecond)

	stats := pool.GetStats()
	t.Logf("健康检查后连接数: %d", stats.TotalConnections)

	// 健康检查不应该移除健康的连接
	if stats.TotalConnections < int64(config.MinConnections) {
		t.Errorf("健康检查不应该移除健康连接，期望至少: %d, 实际: %d", config.MinConnections, stats.TotalConnections)
	}
}

// TestConnectionPool_ConnectionLifecycle 测试连接生命周期管理
func TestConnectionPool_ConnectionLifecycle(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 1
	config.MaxConnections = 5
	config.InitialSize = 1
	config.IdleTimeout = 200 * time.Millisecond
	config.MaxLifetime = 500 * time.Millisecond
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	pool := NewConnectionPool(config, "node-1", "shard-001", "localhost:8080", factory)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err := pool.Start(ctx)
	if err != nil {
		t.Fatalf("启动连接池失败: %v", err)
	}
	defer pool.Stop()

	initialStats := pool.GetStats()
	t.Logf("初始连接数: %d", initialStats.TotalConnections)

	// 等待连接过期
	time.Sleep(1 * time.Second)

	finalStats := pool.GetStats()
	t.Logf("清理后连接数: %d", finalStats.TotalConnections)

	// 验证过期连接被清理（但不低于最小连接数）
	if finalStats.TotalConnections < int64(config.MinConnections) {
		t.Errorf("清理后连接数不应该低于最小连接数，期望至少: %d, 实际: %d", config.MinConnections, finalStats.TotalConnections)
	}
}

// TestConnectionPool_Resize 测试连接池大小调整
func TestConnectionPool_Resize(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 2
	config.MaxConnections = 10
	config.InitialSize = 3
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	pool := NewConnectionPool(config, "node-1", "shard-001", "localhost:8080", factory)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err := pool.Start(ctx)
	if err != nil {
		t.Fatalf("启动连接池失败: %v", err)
	}
	defer pool.Stop()

	initialStats := pool.GetStats()
	t.Logf("初始连接数: %d", initialStats.TotalConnections)

	// 扩大连接池
	err = pool.Resize(7)
	if err != nil {
		t.Fatalf("扩大连接池失败: %v", err)
	}

	time.Sleep(100 * time.Millisecond)
	expandedStats := pool.GetStats()
	t.Logf("扩大后连接数: %d", expandedStats.TotalConnections)

	if expandedStats.TotalConnections < 7 {
		t.Errorf("扩大后连接数不正确，期望至少: 7, 实际: %d", expandedStats.TotalConnections)
	}

	// 缩小连接池
	err = pool.Resize(4)
	if err != nil {
		t.Fatalf("缩小连接池失败: %v", err)
	}

	time.Sleep(100 * time.Millisecond)
	shrunkStats := pool.GetStats()
	t.Logf("缩小后连接数: %d", shrunkStats.TotalConnections)

	if shrunkStats.TotalConnections > expandedStats.TotalConnections {
		t.Errorf("缩小后连接数应该减少")
	}
}

// TestShardAwareConnectionPool_BasicOperations 测试分片感知连接池基本操作
func TestShardAwareConnectionPool_BasicOperations(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 1
	config.MaxConnections = 5
	config.InitialSize = 1
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	shardPool := NewShardAwareConnectionPool(config, factory)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err := shardPool.Start(ctx)
	if err != nil {
		t.Fatalf("启动分片感知连接池失败: %v", err)
	}
	defer shardPool.Stop()

	// 准备分片信息
	shardInfo := &ShardInfo{
		ID:       "shard-001",
		Primary:  NodeID("node-1"),
		Replicas: []NodeID{NodeID("node-2")},
		State:    ShardStateActive,
		Version:  1,
	}

	// 添加分片
	err = shardPool.AddShard(shardInfo)
	if err != nil {
		t.Fatalf("添加分片失败: %v", err)
	}

	// 获取连接
	conn, err := shardPool.GetConnection(ctx, shardInfo, RoutingWritePrimary)
	if err != nil {
		t.Fatalf("获取连接失败: %v", err)
	}
	if conn == nil {
		t.Fatal("获取的连接不应该为空")
	}

	// 归还连接
	shardPool.PutConnection(conn)

	// 验证统计信息
	stats := shardPool.GetStats()
	if stats.TotalShards != 1 {
		t.Errorf("分片数量不正确，期望: 1, 实际: %d", stats.TotalShards)
	}

	// 移除分片
	err = shardPool.RemoveShard(shardInfo.ID)
	if err != nil {
		t.Fatalf("移除分片失败: %v", err)
	}

	// 验证分片已移除
	finalStats := shardPool.GetStats()
	if finalStats.TotalShards != 0 {
		t.Errorf("移除分片后分片数量应该为0，实际: %d", finalStats.TotalShards)
	}
}

// TestShardAwareConnectionPool_MultipleShards 测试多分片管理
func TestShardAwareConnectionPool_MultipleShards(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 1
	config.MaxConnections = 5
	config.InitialSize = 1
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	shardPool := NewShardAwareConnectionPool(config, factory)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err := shardPool.Start(ctx)
	if err != nil {
		t.Fatalf("启动分片感知连接池失败: %v", err)
	}
	defer shardPool.Stop()

	// 准备多个分片
	shards := []*ShardInfo{
		{
			ID:      "shard-001",
			Primary: NodeID("node-1"),
			State:   ShardStateActive,
			Version: 1,
		},
		{
			ID:      "shard-002",
			Primary: NodeID("node-2"),
			State:   ShardStateActive,
			Version: 1,
		},
		{
			ID:      "shard-003",
			Primary: NodeID("node-3"),
			State:   ShardStateActive,
			Version: 1,
		},
	}

	// 添加所有分片
	for _, shard := range shards {
		err = shardPool.AddShard(shard)
		if err != nil {
			t.Fatalf("添加分片 %s 失败: %v", shard.ID, err)
		}
	}

	// 验证分片数量
	stats := shardPool.GetStats()
	if stats.TotalShards != len(shards) {
		t.Errorf("分片数量不正确，期望: %d, 实际: %d", len(shards), stats.TotalShards)
	}

	// 从每个分片获取连接
	connections := make([]*Connection, 0, len(shards))
	for _, shard := range shards {
		conn, err := shardPool.GetConnection(ctx, shard, RoutingWritePrimary)
		if err != nil {
			t.Fatalf("从分片 %s 获取连接失败: %v", shard.ID, err)
		}
		connections = append(connections, conn)
	}

	// 验证连接来自不同的分片
	shardIDs := make(map[string]bool)
	for _, conn := range connections {
		stats := conn.GetStats()
		shardIDs[stats.ShardID] = true
	}

	if len(shardIDs) != len(shards) {
		t.Errorf("连接应该来自不同的分片，期望: %d, 实际: %d", len(shards), len(shardIDs))
	}

	// 归还所有连接
	for _, conn := range connections {
		shardPool.PutConnection(conn)
	}
}

// TestShardAwareConnectionPool_RoutingStrategies 测试路由策略
func TestShardAwareConnectionPool_RoutingStrategies(t *testing.T) {
	config := DefaultPoolConfig()
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	shardPool := NewShardAwareConnectionPool(config, factory)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err := shardPool.Start(ctx)
	if err != nil {
		t.Fatalf("启动分片感知连接池失败: %v", err)
	}
	defer shardPool.Stop()

	// 准备有副本的分片
	shardInfo := &ShardInfo{
		ID:       "shard-001",
		Primary:  NodeID("node-1"),
		Replicas: []NodeID{NodeID("node-2"), NodeID("node-3")},
		State:    ShardStateActive,
		Version:  1,
	}

	err = shardPool.AddShard(shardInfo)
	if err != nil {
		t.Fatalf("添加分片失败: %v", err)
	}

	// 测试写请求路由到主节点
	writeConn, err := shardPool.GetConnection(ctx, shardInfo, RoutingWritePrimary)
	if err != nil {
		t.Fatalf("获取写连接失败: %v", err)
	}

	writeStats := writeConn.GetStats()
	if writeStats.NodeID != shardInfo.Primary {
		t.Errorf("写请求应该路由到主节点，期望: %s, 实际: %s", shardInfo.Primary, writeStats.NodeID)
	}

	shardPool.PutConnection(writeConn)

	// 测试读请求路由到副本节点
	readConn, err := shardPool.GetConnection(ctx, shardInfo, RoutingReadReplica)
	if err != nil {
		t.Fatalf("获取读连接失败: %v", err)
	}

	readStats := readConn.GetStats()

	// 验证路由到了有效节点（主节点或副本节点）
	validNodes := append([]NodeID{shardInfo.Primary}, shardInfo.Replicas...)
	isValidNode := false
	for _, validNode := range validNodes {
		if readStats.NodeID == validNode {
			isValidNode = true
			break
		}
	}

	if !isValidNode {
		t.Errorf("读请求路由到了无效节点: %s", readStats.NodeID)
	}

	shardPool.PutConnection(readConn)
}

// BenchmarkConnectionPool_GetPut 性能测试：连接获取和归还
func BenchmarkConnectionPool_GetPut(b *testing.B) {
	config := DefaultPoolConfig()
	config.MinConnections = 10
	config.MaxConnections = 100
	config.InitialSize = 20
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	pool := NewConnectionPool(config, "node-1", "shard-001", "localhost:8080", factory)

	ctx := context.Background()
	pool.Start(ctx)
	defer pool.Stop()

	// 等待初始化完成
	time.Sleep(100 * time.Millisecond)

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			conn, err := pool.Get(ctx)
			if err != nil {
				b.Fatalf("获取连接失败: %v", err)
			}
			pool.Put(conn)
		}
	})
}

// BenchmarkShardAwareConnectionPool_GetConnection 性能测试：分片感知连接获取
func BenchmarkShardAwareConnectionPool_GetConnection(b *testing.B) {
	config := DefaultPoolConfig()
	config.MinConnections = 5
	config.MaxConnections = 50
	config.InitialSize = 10
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	shardPool := NewShardAwareConnectionPool(config, factory)

	ctx := context.Background()
	shardPool.Start(ctx)
	defer shardPool.Stop()

	// 准备测试分片
	shardInfo := &ShardInfo{
		ID:      "shard-001",
		Primary: NodeID("node-1"),
		State:   ShardStateActive,
		Version: 1,
	}

	shardPool.AddShard(shardInfo)
	time.Sleep(100 * time.Millisecond) // 等待初始化

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			conn, err := shardPool.GetConnection(ctx, shardInfo, RoutingWritePrimary)
			if err != nil {
				b.Fatalf("获取连接失败: %v", err)
			}
			shardPool.PutConnection(conn)
		}
	})
}

// TestConnectionPool_ConcurrentAccess 并发访问测试
func TestConnectionPool_ConcurrentAccess(t *testing.T) {
	config := DefaultPoolConfig()
	config.MinConnections = 10
	config.MaxConnections = 100 // 增加最大连接数
	config.InitialSize = 20     // 增加初始连接数
	config.EnablePreWarm = false

	factory := NewMockConnectionFactory(5 * time.Second)
	pool := NewConnectionPool(config, "node-1", "shard-001", "localhost:8080", factory)

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	err := pool.Start(ctx)
	if err != nil {
		t.Fatalf("启动连接池失败: %v", err)
	}
	defer pool.Stop()

	// 等待初始化完成
	time.Sleep(200 * time.Millisecond)

	// 并发测试参数 - 降低并发度，增加操作数
	concurrency := 20            // 减少并发goroutine数
	operationsPerGoroutine := 50 // 减少每个goroutine的操作数
	var wg sync.WaitGroup
	var errors []error
	var errorMu sync.Mutex

	for i := 0; i < concurrency; i++ {
		wg.Add(1)
		go func(goroutineID int) {
			defer wg.Done()

			for j := 0; j < operationsPerGoroutine; j++ {
				// 获取连接
				conn, err := pool.Get(ctx)
				if err != nil {
					errorMu.Lock()
					errors = append(errors, fmt.Errorf("goroutine %d, operation %d, get: %w", goroutineID, j, err))
					errorMu.Unlock()
					continue
				}

				// 模拟使用连接
				time.Sleep(1 * time.Millisecond)

				// 归还连接
				pool.Put(conn)
			}
		}(i)
	}

	wg.Wait()

	if len(errors) > 0 {
		t.Fatalf("并发测试发现 %d 个错误，第一个错误: %v", len(errors), errors[0])
	}

	// 验证最终状态
	stats := pool.GetStats()
	expectedOperations := int64(concurrency * operationsPerGoroutine)
	if stats.TotalRequests < expectedOperations {
		t.Errorf("总请求数不正确，期望至少: %d, 实际: %d", expectedOperations, stats.TotalRequests)
	}

	t.Logf("并发测试完成：%d个goroutine，每个%d次操作，总计请求数: %d", concurrency, operationsPerGoroutine, stats.TotalRequests)
}
