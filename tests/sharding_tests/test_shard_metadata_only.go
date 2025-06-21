/*
* @Author: Lzww0608
* @Date: 2025-6-21 21:21:06
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-21 21:21:06
* @Description: ConcordKV shard metadata management tests
 */

package main

import (
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

// MockShardMetadataStorage2 模拟存储实现 (避免重复声明)
type MockShardMetadataStorage2 struct {
	shards   map[string]*sharding.ShardInfo
	metadata map[string][]byte
}

func NewMockShardMetadataStorage2() *MockShardMetadataStorage2 {
	return &MockShardMetadataStorage2{
		shards:   make(map[string]*sharding.ShardInfo),
		metadata: make(map[string][]byte),
	}
}

func (m *MockShardMetadataStorage2) SaveShardInfo(shard *sharding.ShardInfo) error {
	// 创建深拷贝
	shardCopy := *shard
	m.shards[shard.ID] = &shardCopy
	return nil
}

func (m *MockShardMetadataStorage2) LoadShardInfo(shardID string) (*sharding.ShardInfo, error) {
	shard, exists := m.shards[shardID]
	if !exists {
		return nil, fmt.Errorf("分片 %s 不存在", shardID)
	}
	shardCopy := *shard
	return &shardCopy, nil
}

func (m *MockShardMetadataStorage2) LoadAllShards() (map[string]*sharding.ShardInfo, error) {
	// 如果没有分片数据，返回错误以触发初始化
	if len(m.shards) == 0 {
		return nil, fmt.Errorf("没有找到分片数据")
	}

	result := make(map[string]*sharding.ShardInfo)
	for id, shard := range m.shards {
		shardCopy := *shard
		result[id] = &shardCopy
	}
	return result, nil
}

func (m *MockShardMetadataStorage2) DeleteShardInfo(shardID string) error {
	delete(m.shards, shardID)
	return nil
}

func (m *MockShardMetadataStorage2) SaveMetadata(key string, value []byte) error {
	m.metadata[key] = make([]byte, len(value))
	copy(m.metadata[key], value)
	return nil
}

func (m *MockShardMetadataStorage2) LoadMetadata(key string) ([]byte, error) {
	value, exists := m.metadata[key]
	if !exists {
		return nil, fmt.Errorf("元数据 %s 不存在", key)
	}
	result := make([]byte, len(value))
	copy(result, value)
	return result, nil
}

// 辅助函数
func mockNodeID2(id string) raft.NodeID {
	return raft.NodeID(id)
}

func createTestHashRing2() *sharding.ConsistentHashRing {
	ring := sharding.NewConsistentHashRing(nil)

	// 添加测试节点
	nodes := []raft.NodeID{
		mockNodeID2("node1"),
		mockNodeID2("node2"),
		mockNodeID2("node3"),
	}

	for i, nodeID := range nodes {
		ring.AddNode(nodeID, fmt.Sprintf("127.0.0.1:800%d", i+1), 1.0)
	}

	return ring
}

// TestShardMetadataManagerBasic 测试基本功能
func TestShardMetadataManagerBasic(t TestInterface) {
	fmt.Println("=== 测试分片元数据管理器基本功能 ===")

	// 创建组件
	storage := NewMockShardMetadataStorage2()
	hashRing := createTestHashRing2()
	config := sharding.DefaultShardMetadataConfig()
	config.ShardCount = 4 // 减少分片数量便于测试

	manager := sharding.NewShardMetadataManager(config, hashRing, storage)

	// 获取统计信息
	stats := manager.GetStats()
	fmt.Printf("初始分片数: %d, 活跃分片数: %d\n", stats.TotalShards, stats.ActiveShards)

	// 应该有初始化的分片
	if stats.TotalShards != config.ShardCount {
		t.Errorf("期望分片数为%d，实际为%d", config.ShardCount, stats.TotalShards)
	}

	// 获取所有分片
	shards := manager.GetAllShards()
	fmt.Println("初始分片列表:")
	for shardID, shard := range shards {
		fmt.Printf("  %s: 范围[%d-%d], 主节点=%s, 副本=%v\n",
			shardID, shard.Range.StartHash, shard.Range.EndHash,
			shard.Primary, shard.Replicas)
	}

	fmt.Println("✓ 基本功能测试通过")
}

// TestShardMetadataManagerCreateShard 测试分片创建
func TestShardMetadataManagerCreateShard(t TestInterface) {
	fmt.Println("\n=== 测试分片创建 ===")

	storage := NewMockShardMetadataStorage2()
	hashRing := createTestHashRing2()
	config := sharding.DefaultShardMetadataConfig()
	config.ShardCount = 0 // 不创建默认分片

	manager := sharding.NewShardMetadataManager(config, hashRing, storage)

	// 创建新分片
	shardID := "test-shard-001"
	shardRange := sharding.ShardRange{
		StartHash: 0,
		EndHash:   1000,
	}
	primary := mockNodeID2("node1")
	replicas := []raft.NodeID{mockNodeID2("node2"), mockNodeID2("node3")}

	err := manager.CreateShard(shardID, shardRange, primary, replicas)
	if err != nil {
		t.Fatalf("创建分片失败: %v", err)
	}
	fmt.Printf("✓ 创建分片 %s 成功\n", shardID)

	// 验证分片信息
	shard, err := manager.GetShard(shardID)
	if err != nil {
		t.Fatalf("获取分片失败: %v", err)
	}

	if shard.ID != shardID {
		t.Errorf("分片ID不匹配: 期望%s, 实际%s", shardID, shard.ID)
	}

	if shard.Primary != primary {
		t.Errorf("主节点不匹配: 期望%s, 实际%s", primary, shard.Primary)
	}

	if len(shard.Replicas) != len(replicas) {
		t.Errorf("副本数量不匹配: 期望%d, 实际%d", len(replicas), len(shard.Replicas))
	}

	// 测试重复创建
	err = manager.CreateShard(shardID, shardRange, primary, replicas)
	if err == nil {
		t.Error("重复创建分片应该失败")
	}
	fmt.Println("✓ 重复创建检测正常")

	fmt.Println("✓ 分片创建测试通过")
}

// TestShardMetadataManagerKeyRouting 测试键路由
func TestShardMetadataManagerKeyRouting(t TestInterface) {
	fmt.Println("\n=== 测试键路由 ===")

	storage := NewMockShardMetadataStorage2()
	hashRing := createTestHashRing2()
	config := sharding.DefaultShardMetadataConfig()
	config.ShardCount = 4

	manager := sharding.NewShardMetadataManager(config, hashRing, storage)

	// 测试键路由
	testKeys := []string{
		"user:1001",
		"user:1002",
		"product:2001",
		"order:3001",
	}

	for _, key := range testKeys {
		shard, err := manager.GetShardByKey(key)
		if err != nil {
			t.Fatalf("路由键 %s 失败: %v", key, err)
		}

		fmt.Printf("键 %s -> 分片 %s (主节点: %s)\n",
			key, shard.ID, shard.Primary)
	}

	fmt.Println("✓ 键路由测试通过")
}

// BenchmarkShardMetadataManagerRouting 路由性能基准测试
func BenchmarkShardMetadataManagerRouting(b *testing.B) {
	storage := NewMockShardMetadataStorage2()
	hashRing := createTestHashRing2()
	config := sharding.DefaultShardMetadataConfig()

	manager := sharding.NewShardMetadataManager(config, hashRing, storage)

	// 准备测试键
	keys := make([]string, 1000)
	for i := 0; i < 1000; i++ {
		keys[i] = fmt.Sprintf("benchmark:key:%d", i)
	}

	b.ResetTimer()
	b.ReportAllocs()

	for i := 0; i < b.N; i++ {
		key := keys[i%len(keys)]
		_, err := manager.GetShardByKey(key)
		if err != nil {
			b.Fatalf("路由失败: %v", err)
		}
	}
}

// main2 函数运行所有测试
func main() {
	fmt.Println("开始运行分片元数据管理器测试...")

	// 创建测试运行器
	tr := &TestRunner{}

	// 运行所有测试，使用defer recover处理panic
	defer func() {
		if r := recover(); r != nil {
			fmt.Printf("测试因严重错误而终止: %v\n", r)
		}
	}()

	TestShardMetadataManagerBasic(tr)
	TestShardMetadataManagerCreateShard(tr)
	TestShardMetadataManagerKeyRouting(tr)

	if tr.Failed() {
		fmt.Println("\n❌ 部分测试失败")
		return
	}

	fmt.Println("\n✅ 所有测试通过！")

	// 运行性能基准测试
	fmt.Println("\n开始性能基准测试...")
	result := testing.Benchmark(BenchmarkShardMetadataManagerRouting)
	fmt.Printf("性能测试结果: %s\n", result.String())
}
