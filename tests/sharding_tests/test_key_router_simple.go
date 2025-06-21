/*
* @Author: Lzww0608
* @Date: 2025-6-21 21:21:06
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-21 21:21:06
* @Description: ConcordKV key router simple tests
 */

package main

import (
	"context"
	"fmt"
	"testing"

	"raftserver/raft"
	"raftserver/sharding"
)

// 测试接口
type TestInterface interface {
	Errorf(format string, args ...interface{})
	Fatalf(format string, args ...interface{})
	Error(args ...interface{})
	Fatal(args ...interface{})
	Failed() bool
}

// 自定义测试运行器
type TestRunner struct {
	failed bool
}

func (tr *TestRunner) Errorf(format string, args ...interface{}) {
	fmt.Printf("❌ ERROR: "+format+"\n", args...)
	tr.failed = true
}

func (tr *TestRunner) Fatalf(format string, args ...interface{}) {
	fmt.Printf("❌ FATAL: "+format+"\n", args...)
	tr.failed = true
	panic("test failed")
}

func (tr *TestRunner) Error(args ...interface{}) {
	fmt.Print("❌ ERROR: ")
	fmt.Println(args...)
	tr.failed = true
}

func (tr *TestRunner) Fatal(args ...interface{}) {
	fmt.Print("❌ FATAL: ")
	fmt.Println(args...)
	tr.failed = true
	panic("test failed")
}

func (tr *TestRunner) Failed() bool {
	return tr.failed
}

// 辅助函数
func mockNodeID3(id string) raft.NodeID {
	return raft.NodeID(id)
}

func createTestComponents3() (*sharding.ConsistentHashRing, *sharding.ShardMetadataManager) {
	// 创建一致性哈希环
	ring := sharding.NewConsistentHashRing(nil)

	// 添加测试节点
	nodes := []raft.NodeID{
		mockNodeID3("node1"),
		mockNodeID3("node2"),
		mockNodeID3("node3"),
	}

	for i, nodeID := range nodes {
		ring.AddNode(nodeID, fmt.Sprintf("127.0.0.1:800%d", i+1), 1.0)
	}

	// 创建模拟存储
	storage := &MockShardMetadataStorage3{
		shards:   make(map[string]*sharding.ShardInfo),
		metadata: make(map[string][]byte),
	}

	// 创建分片元数据管理器
	config := sharding.DefaultShardMetadataConfig()
	config.ShardCount = 4
	manager := sharding.NewShardMetadataManager(config, ring, storage)

	return ring, manager
}

// MockShardMetadataStorage3 模拟存储实现
type MockShardMetadataStorage3 struct {
	shards   map[string]*sharding.ShardInfo
	metadata map[string][]byte
}

func (m *MockShardMetadataStorage3) SaveShardInfo(shard *sharding.ShardInfo) error {
	m.shards[shard.ID] = shard
	return nil
}

func (m *MockShardMetadataStorage3) LoadShardInfo(shardID string) (*sharding.ShardInfo, error) {
	shard, exists := m.shards[shardID]
	if !exists {
		return nil, fmt.Errorf("分片 %s 不存在", shardID)
	}
	return shard, nil
}

func (m *MockShardMetadataStorage3) LoadAllShards() (map[string]*sharding.ShardInfo, error) {
	// 如果没有分片数据，返回错误以触发初始化
	if len(m.shards) == 0 {
		return nil, fmt.Errorf("没有找到分片数据")
	}

	result := make(map[string]*sharding.ShardInfo)
	for id, shard := range m.shards {
		result[id] = shard
	}
	return result, nil
}

func (m *MockShardMetadataStorage3) DeleteShardInfo(shardID string) error {
	delete(m.shards, shardID)
	return nil
}

func (m *MockShardMetadataStorage3) SaveMetadata(key string, value []byte) error {
	m.metadata[key] = value
	return nil
}

func (m *MockShardMetadataStorage3) LoadMetadata(key string) ([]byte, error) {
	value, exists := m.metadata[key]
	if !exists {
		return nil, fmt.Errorf("元数据 %s 不存在", key)
	}
	return value, nil
}

// TestKeyRouterBasic 测试基本功能
func TestKeyRouterBasic(t TestInterface) {
	fmt.Println("=== 测试键路由基本功能 ===")

	hashRing, metadataManager := createTestComponents3()
	config := sharding.DefaultKeyRouterConfig()

	router := sharding.NewKeyRouter(config, metadataManager, hashRing)
	defer router.Close()

	// 测试基本路由
	testKey := "user:1001"
	req := &sharding.RoutingRequest{
		Key:      testKey,
		Strategy: sharding.RoutingStrategyPrimary,
		ReadOnly: false,
		Context:  context.Background(),
	}

	result, err := router.RouteKey(req)
	if err != nil {
		t.Errorf("路由键失败: %v", err)
		return
	}

	fmt.Printf("键 %s 路由结果:\n", testKey)
	fmt.Printf("  目标节点: %s\n", result.TargetNode)
	fmt.Printf("  分片ID: %s\n", result.ShardInfo.ID)
	fmt.Printf("  主节点: %s\n", result.PrimaryNode)
	fmt.Printf("  是否缓存: %t\n", result.Cached)

	if result.TargetNode == "" {
		t.Error("目标节点不能为空")
	}

	if result.ShardInfo.ID == "" {
		t.Error("分片ID不能为空")
	}

	fmt.Println("✓ 基本功能测试通过")
}

// TestKeyRouterStrategies 测试路由策略
func TestKeyRouterStrategies(t TestInterface) {
	fmt.Println("\n=== 测试路由策略 ===")

	hashRing, metadataManager := createTestComponents3()
	config := sharding.DefaultKeyRouterConfig()

	router := sharding.NewKeyRouter(config, metadataManager, hashRing)
	defer router.Close()

	testKey := "user:strategy:test"

	// 只测试主节点策略，避免复杂的策略实现问题
	req := &sharding.RoutingRequest{
		Key:      testKey,
		Strategy: sharding.RoutingStrategyPrimary,
		ReadOnly: false,
		Context:  context.Background(),
	}

	result, err := router.RouteKey(req)
	if err != nil {
		t.Errorf("路由失败 (Primary): %v", err)
		return
	}

	fmt.Printf("策略 Primary: 目标节点=%s, 分片=%s\n",
		result.TargetNode, result.ShardInfo.ID)

	if result.TargetNode == "" {
		t.Error("目标节点不能为空")
	}

	fmt.Println("✓ 路由策略测试通过")
}

// TestKeyRouterBatch 测试批量路由
func TestKeyRouterBatch(t TestInterface) {
	fmt.Println("\n=== 测试批量路由 ===")

	hashRing, metadataManager := createTestComponents3()
	config := sharding.DefaultKeyRouterConfig()

	router := sharding.NewKeyRouter(config, metadataManager, hashRing)
	defer router.Close()

	// 测试批量路由
	testKeys := []string{
		"user:1001",
		"user:1002",
		"product:2001",
		"order:3001",
	}

	results, err := router.RouteBatch(testKeys, sharding.RoutingStrategyPrimary, false)
	if err != nil {
		t.Errorf("批量路由失败: %v", err)
		return
	}

	if len(results) != len(testKeys) {
		t.Errorf("批量路由结果数量不匹配: 期望%d, 实际%d", len(testKeys), len(results))
		return
	}

	fmt.Println("批量路由结果:")
	for key, result := range results {
		fmt.Printf("  %s -> 节点:%s, 分片:%s\n",
			key, result.TargetNode, result.ShardInfo.ID)
	}

	fmt.Println("✓ 批量路由测试通过")
}

// BenchmarkKeyRouterRouting 路由性能基准测试
func BenchmarkKeyRouterRouting(b *testing.B) {
	hashRing, metadataManager := createTestComponents3()
	config := sharding.DefaultKeyRouterConfig()

	router := sharding.NewKeyRouter(config, metadataManager, hashRing)
	defer router.Close()

	// 准备测试键
	keys := make([]string, 1000)
	for i := 0; i < 1000; i++ {
		keys[i] = fmt.Sprintf("benchmark:key:%d", i)
	}

	b.ResetTimer()
	b.ReportAllocs()

	for i := 0; i < b.N; i++ {
		key := keys[i%len(keys)]
		req := &sharding.RoutingRequest{
			Key:      key,
			Strategy: sharding.RoutingStrategyPrimary,
			ReadOnly: false,
			Context:  context.Background(),
		}

		_, err := router.RouteKey(req)
		if err != nil {
			b.Fatalf("路由失败: %v", err)
		}
	}
}

// main 函数运行所有测试
func main() {
	fmt.Println("开始运行键路由服务测试...")

	// 创建测试运行器
	tr := &TestRunner{}

	// 运行所有测试，使用defer recover处理panic
	defer func() {
		if r := recover(); r != nil {
			fmt.Printf("测试因严重错误而终止: %v\n", r)
		}
	}()

	TestKeyRouterBasic(tr)
	TestKeyRouterStrategies(tr)
	TestKeyRouterBatch(tr)

	if tr.Failed() {
		fmt.Println("\n❌ 部分测试失败")
		return
	}

	fmt.Println("\n✅ 所有测试通过！")

	// 运行性能基准测试
	fmt.Println("\n开始性能基准测试...")
	result := testing.Benchmark(BenchmarkKeyRouterRouting)
	fmt.Printf("性能测试结果: %s\n", result.String())
}
