package main

import (
	"fmt"
	"testing"
	"time"
)

// TestTopologyBasics 基础功能测试
func TestTopologyBasics(t *testing.T) {
	service := NewSimpleTopologyService()
	
	// 测试1: 获取初始快照
	snapshot, err := service.GetTopologySnapshot()
	if err != nil {
		t.Fatalf("获取拓扑快照失败: %v", err)
	}
	
	if snapshot.Version != 1 {
		t.Errorf("期望版本=1, 实际=%d", snapshot.Version)
	}
	
	if len(snapshot.NodeStatus) != 3 {
		t.Errorf("期望节点数=3, 实际=%d", len(snapshot.NodeStatus))
	}
	
	if len(snapshot.ShardMap) != 3 {
		t.Errorf("期望分片数=3, 实际=%d", len(snapshot.ShardMap))
	}
	
	t.Logf("✅ 初始快照测试通过: 版本=%d, 节点=%d, 分片=%d", 
		snapshot.Version, len(snapshot.NodeStatus), len(snapshot.ShardMap))
}

// TestHealthUpdate 健康状态更新测试
func TestHealthUpdate(t *testing.T) {
	service := NewSimpleTopologyService()
	
	// 获取初始状态
	snapshot1, _ := service.GetTopologySnapshot()
	initialVersion := snapshot1.Version
	
	// 更新节点健康状态
	service.UpdateNodeHealth(NodeID("node-1"), HealthStatusUnhealthy)
	
	// 验证更新
	snapshot2, err := service.GetTopologySnapshot()
	if err != nil {
		t.Fatalf("获取更新后快照失败: %v", err)
	}
	
	if snapshot2.Version <= initialVersion {
		t.Errorf("期望版本递增, 初始=%d, 更新后=%d", initialVersion, snapshot2.Version)
	}
	
	node1 := snapshot2.NodeStatus[NodeID("node-1")]
	if node1.Health != HealthStatusUnhealthy {
		t.Errorf("期望node-1健康状态=Unhealthy, 实际=%v", node1.Health)
	}
	
	t.Logf("✅ 健康状态更新测试通过: 版本 %d -> %d", initialVersion, snapshot2.Version)
}

// TestShardMapping 分片映射测试
func TestShardMapping(t *testing.T) {
	service := NewSimpleTopologyService()
	snapshot, _ := service.GetTopologySnapshot()
	
	// 验证分片映射
	expectedShards := map[string]struct{}{
		"shard-1": {},
		"shard-2": {},
		"shard-3": {},
	}
	
	for shardID := range snapshot.ShardMap {
		if _, exists := expectedShards[shardID]; !exists {
			t.Errorf("发现意外的分片: %s", shardID)
		}
		delete(expectedShards, shardID)
	}
	
	if len(expectedShards) > 0 {
		t.Errorf("缺少分片: %v", expectedShards)
	}
	
	// 验证键范围覆盖
	shard1 := snapshot.ShardMap["shard-1"]
	if shard1.KeyRange.Start != "a" || shard1.KeyRange.End != "h" {
		t.Errorf("shard-1键范围错误: %s-%s", shard1.KeyRange.Start, shard1.KeyRange.End)
	}
	
	t.Logf("✅ 分片映射测试通过: 分片数=%d", len(snapshot.ShardMap))
}

// TestConcurrency 并发安全测试
func TestConcurrency(t *testing.T) {
	service := NewSimpleTopologyService()
	
	// 并发读写测试
	done := make(chan bool, 10)
	
	// 5个读操作
	for i := 0; i < 5; i++ {
		go func(id int) {
			for j := 0; j < 100; j++ {
				_, err := service.GetTopologySnapshot()
				if err != nil {
					t.Errorf("并发读取失败 (goroutine %d): %v", id, err)
				}
			}
			done <- true
		}(i)
	}
	
	// 5个写操作
	for i := 0; i < 5; i++ {
		go func(id int) {
			for j := 0; j < 20; j++ {
				nodeID := NodeID(fmt.Sprintf("node-%d", (id%3)+1))
				health := HealthStatus(j % 4)
				service.UpdateNodeHealth(nodeID, health)
				time.Sleep(time.Millisecond)
			}
			done <- true
		}(i)
	}
	
	// 等待所有goroutine完成
	for i := 0; i < 10; i++ {
		<-done
	}
	
	t.Logf("✅ 并发安全测试通过: 10个goroutine并发执行完成")
}

// BenchmarkSnapshot 快照性能基准测试
func BenchmarkSnapshot(b *testing.B) {
	service := NewSimpleTopologyService()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, err := service.GetTopologySnapshot()
		if err != nil {
			b.Fatalf("获取快照失败: %v", err)
		}
	}
}

// BenchmarkHealthUpdate 健康更新性能基准测试
func BenchmarkHealthUpdate(b *testing.B) {
	service := NewSimpleTopologyService()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		nodeID := NodeID(fmt.Sprintf("node-%d", (i%3)+1))
		health := HealthStatus(i % 4)
		service.UpdateNodeHealth(nodeID, health)
	}
}

// 运行所有测试的主函数
func runUnitTests() {
	fmt.Println("🧪 运行单元测试...")
	
	// 手动运行测试
	tests := []struct{
		name string
		fn   func(*testing.T)
	}{
		{"基础功能测试", TestTopologyBasics},
		{"健康状态更新测试", TestHealthUpdate},
		{"分片映射测试", TestShardMapping},
		{"并发安全测试", TestConcurrency},
	}
	
	for _, test := range tests {
		t := &testing.T{}
		fmt.Printf("  🔍 %s...\n", test.name)
		test.fn(t)
		if t.Failed() {
			fmt.Printf("    ❌ %s失败\n", test.name)
		} else {
			fmt.Printf("    ✅ %s通过\n", test.name)
		}
	}
	
	fmt.Println("\n🎯 单元测试完成!")
}
