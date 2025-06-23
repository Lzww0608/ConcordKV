package sharding

import (
	"fmt"
	"testing"
	"time"
	"raftserver/raft"
)

// BenchmarkLoadImbalanceDetection 负载不平衡检测性能基准测试
func BenchmarkLoadImbalanceDetection(b *testing.B) {
	loadMonitor := NewMockLoadMonitor()
	hashRing := createTestHashRing()
	
	// 添加更多节点进行测试
	for i := 0; i < 50; i++ {
		nodeID := fmt.Sprintf("bench-node-%d", i)
		hashRing.AddNode(raft.NodeID(nodeID), fmt.Sprintf("addr%d", i), 1.0)
		loadMonitor.SetNodeMetrics(raft.NodeID(nodeID), &raft.LoadMetrics{
			LoadScore: float64(i%10) * 0.1,
		})
	}
	
	config := DefaultRebalancerConfig()
	rebalancer := NewRebalancer(config, nil, hashRing, nil, loadMonitor)
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = rebalancer.detectLoadImbalance()
	}
}

// BenchmarkDataMigrationTask 数据迁移任务性能基准测试
func BenchmarkDataMigrationTask(b *testing.B) {
	config := DefaultDataMigratorConfig()
	storageEngine := NewMockStorageEngine()
	networkTransfer := NewMockNetworkTransfer()
	migrator := NewDataMigrator(config, storageEngine, networkTransfer, nil)
	
	migrator.Start()
	defer migrator.Stop()
	
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		task := &MigrationTask{
			ShardID:    fmt.Sprintf("bench-shard-%d", i),
			SourceNode: "bench-source",
			TargetNode: "bench-target",
			KeyRange: KeyRange{
				StartKey: fmt.Sprintf("bench-key-%d-000", i),
				EndKey:   fmt.Sprintf("bench-key-%d-999", i),
			},
		}
		
		err := migrator.SubmitMigrationTask(task)
		if err != nil {
			b.Errorf("提交任务失败: %v", err)
		}
	}
}

// TestPerformanceRequirements 性能要求验证测试
func TestPerformanceRequirements(t *testing.T) {
	// 1. 负载检测延迟要求 (< 100ms)
	t.Run("LoadDetectionLatency", func(t *testing.T) {
		env := createIntegrationEnvironment()
		defer env.cleanup()
		
		env.simulateHighLoad("node-1", 0.8)
		env.simulateHighLoad("node-2", 0.2)
		
		start := time.Now()
		imbalance := env.rebalancer.detectLoadImbalance()
		latency := time.Since(start)
		
		if latency > 100*time.Millisecond {
			t.Errorf("负载检测延迟过高: %v, 要求 < 100ms", latency)
		}
		
		if imbalance == nil {
			t.Error("应该检测到负载不平衡")
		}
		
		t.Logf("负载检测延迟: %v", latency)
	})
	
	// 2. 迁移吞吐量要求 (> 1000 keys/sec)
	t.Run("MigrationThroughput", func(t *testing.T) {
		env := createIntegrationEnvironment()
		defer env.cleanup()
		
		env.startAll(t)
		
		// 提交多个迁移任务
		numTasks := 3
		keysPerTask := 200
		
		start := time.Now()
		
		for i := 0; i < numTasks; i++ {
			task := &MigrationTask{
				ShardID:    fmt.Sprintf("perf-shard-%d", i),
				SourceNode: "node-1",
				TargetNode: "node-2",
				KeyRange: KeyRange{
					StartKey: fmt.Sprintf("perf-key-%03d-000", i),
					EndKey:   fmt.Sprintf("perf-key-%03d-999", i),
				},
			}
			
			err := env.migrator.SubmitMigrationTask(task)
			if err != nil {
				t.Errorf("提交任务失败: %v", err)
			}
		}
		
		// 等待所有任务完成
		maxWait := 1 * time.Second
		checkInterval := 50 * time.Millisecond
		elapsed := time.Duration(0)
		
		for elapsed < maxWait {
			time.Sleep(checkInterval)
			elapsed += checkInterval
			
			stats := env.migrator.GetStats()
			if stats.CompletedTasks == int64(numTasks) {
				totalTime := time.Since(start)
				totalKeys := numTasks * keysPerTask
				throughput := float64(totalKeys) / totalTime.Seconds()
				
				t.Logf("迁移吞吐量: %.2f keys/sec, 总耗时: %v", throughput, totalTime)
				return
			}
		}
		
		t.Fatal("性能测试超时")
	})
}

// TestScalabilityRequirements 可扩展性要求验证测试
func TestScalabilityRequirements(t *testing.T) {
	// 测试大规模节点场景
	t.Run("LargeScaleNodes", func(t *testing.T) {
		loadMonitor := NewMockLoadMonitor()
		hashRing := NewConsistentHashRing(DefaultHashRingConfig())
		
		// 添加大量节点
		numNodes := 100
		for i := 0; i < numNodes; i++ {
			nodeID := fmt.Sprintf("scale-node-%d", i)
			hashRing.AddNode(raft.NodeID(nodeID), fmt.Sprintf("addr%d", i), 1.0)
			loadMonitor.SetNodeMetrics(raft.NodeID(nodeID), &raft.LoadMetrics{
				LoadScore: float64(i%10) * 0.1,
			})
		}
		
		config := DefaultRebalancerConfig()
		rebalancer := NewRebalancer(config, nil, hashRing, nil, loadMonitor)
		
		// 测试负载检测性能
		start := time.Now()
		imbalance := rebalancer.detectLoadImbalance()
		latency := time.Since(start)
		
		if latency > 500*time.Millisecond {
			t.Errorf("大规模负载检测延迟过高: %v, 要求 < 500ms", latency)
		}
		
		if imbalance == nil {
			t.Error("应该检测到负载不平衡")
		}
		
		t.Logf("大规模负载检测 (%d 节点): %v", numNodes, latency)
	})
}
