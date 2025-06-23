/*
* @Author: Lzww0608
* @Date: 2025-6-23 17:39:34
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-23 17:39:34
* @Description: ConcordKV 5.1.2 动态负载检测和平衡 - 完整系统集成测试
 */

package sharding

import (
	"fmt"
	"raftserver/raft"
	"testing"
	"time"
)

// TestFullSystemIntegration 测试完整系统集成
func TestFullSystemIntegration(t *testing.T) {
	// 创建集成环境
	env := createIntegrationEnvironment()
	defer env.cleanup()

	// 启动所有组件
	env.startAll(t)

	// 模拟负载不平衡场景
	env.simulateHighLoad("node-1", 0.8)
	env.simulateHighLoad("node-2", 0.2)
	env.simulateHighLoad("node-3", 0.3)

	// 等待系统响应
	time.Sleep(100 * time.Millisecond)

	// 验证重平衡检测
	imbalanceInfo := env.rebalancer.detectLoadImbalance()
	if imbalanceInfo == nil {
		t.Fatal("应该检测到负载不平衡")
	}

	if !imbalanceInfo.RequiresRebalance {
		t.Error("应该需要重平衡")
	}

	// 提交数据迁移任务
	migrationTask := &MigrationTask{
		ShardID:    "test-shard-1",
		SourceNode: "node-1",
		TargetNode: "node-2",
		KeyRange: KeyRange{
			StartKey: "key-000",
			EndKey:   "key-999",
		},
		Priority: 5,
	}

	err := env.migrator.SubmitMigrationTask(migrationTask)
	if err != nil {
		t.Errorf("提交迁移任务失败: %v", err)
	}

	// 等待迁移完成
	time.Sleep(200 * time.Millisecond)

	// 验证迁移结果
	taskStatus, err := env.migrator.GetTaskStatus(migrationTask.ID)
	if err != nil {
		t.Errorf("获取任务状态失败: %v", err)
	}

	if taskStatus.Status != TaskStatusCompleted {
		t.Errorf("迁移任务应该完成，实际状态: %s", taskStatus.Status)
	}

	t.Logf("集成测试完成 - 负载不平衡比率: %.3f", imbalanceInfo.ImbalanceRatio)
}

// TestLoadBalanceWorkflow 测试负载均衡工作流
func TestLoadBalanceWorkflow(t *testing.T) {
	env := createIntegrationEnvironment()
	defer env.cleanup()

	env.startAll(t)

	// 模拟高负载场景
	env.simulateHighLoad("node-1", 0.8)
	env.simulateHighLoad("node-2", 0.2)
	env.simulateHighLoad("node-3", 0.3)

	// 等待负载检测
	time.Sleep(100 * time.Millisecond)

	// 验证负载检测结果
	imbalance := env.rebalancer.detectLoadImbalance()
	if imbalance == nil || !imbalance.RequiresRebalance {
		t.Fatal("应该检测到需要重平衡")
	}

	// 验证过载和负载不足节点识别
	if len(imbalance.OverloadedNodes) == 0 {
		t.Error("应该识别出过载节点")
	}

	if len(imbalance.UnderloadedNodes) == 0 {
		t.Error("应该识别出负载不足节点")
	}

	// 执行重平衡
	env.rebalancer.executeRebalance(imbalance)

	// 验证权重调整
	nodes := env.hashRing.GetPhysicalNodes()
	overloadedNode := nodes["node-1"]
	if overloadedNode != nil && overloadedNode.Weight >= 1.0 {
		t.Error("过载节点的权重应该被降低")
	}

	t.Logf("负载均衡工作流测试完成 - 过载节点: %d, 负载不足节点: %d",
		len(imbalance.OverloadedNodes), len(imbalance.UnderloadedNodes))
}

// TestDataMigrationWorkflow 测试数据迁移工作流
func TestDataMigrationWorkflow(t *testing.T) {
	env := createIntegrationEnvironment()
	defer env.cleanup()

	env.startAll(t)

	// 预填充测试数据
	env.populateTestData(100)

	// 创建迁移任务
	task := &MigrationTask{
		ShardID:    "shard-test",
		SourceNode: "node-1",
		TargetNode: "node-2",
		KeyRange: KeyRange{
			StartKey: "test-key-000",
			EndKey:   "test-key-999",
		},
	}

	// 提交任务
	err := env.migrator.SubmitMigrationTask(task)
	if err != nil {
		t.Fatalf("提交迁移任务失败: %v", err)
	}

	// 等待任务完成
	maxWait := 300 * time.Millisecond
	checkInterval := 50 * time.Millisecond
	elapsed := time.Duration(0)

	for elapsed < maxWait {
		time.Sleep(checkInterval)
		elapsed += checkInterval

		taskStatus, err := env.migrator.GetTaskStatus(task.ID)
		if err != nil {
			continue
		}

		if taskStatus.Status == TaskStatusCompleted {
			t.Logf("数据迁移完成 - 处理键: %d, 传输字节: %d",
				taskStatus.Progress.ProcessedKeys,
				taskStatus.Progress.TransferredBytes)
			return
		}

		if taskStatus.Status == TaskStatusFailed {
			t.Fatalf("迁移任务失败: %v", taskStatus.Error)
		}
	}

	t.Fatal("迁移任务超时")
}

// TestErrorHandling 测试错误处理
func TestErrorHandling(t *testing.T) {
	env := createIntegrationEnvironment()
	defer env.cleanup()

	env.startAll(t)

	// 测试无效任务
	invalidTask := &MigrationTask{
		ShardID:    "", // 空分片ID
		SourceNode: "node-1",
		TargetNode: "node-2",
	}

	err := env.migrator.SubmitMigrationTask(invalidTask)
	if err == nil {
		t.Error("应该拒绝无效任务")
	}

	// 测试任务取消
	validTask := &MigrationTask{
		ShardID:    "cancel-test",
		SourceNode: "node-1",
		TargetNode: "node-2",
		KeyRange: KeyRange{
			StartKey: "cancel-000",
			EndKey:   "cancel-999",
		},
	}

	err = env.migrator.SubmitMigrationTask(validTask)
	if err != nil {
		t.Fatalf("提交任务失败: %v", err)
	}

	// 取消任务
	err = env.migrator.CancelTask(validTask.ID)
	if err != nil {
		t.Errorf("取消任务失败: %v", err)
	}

	// 验证任务状态
	time.Sleep(50 * time.Millisecond)
	taskStatus, err := env.migrator.GetTaskStatus(validTask.ID)
	if err != nil {
		t.Errorf("获取任务状态失败: %v", err)
	}

	if taskStatus.Status != TaskStatusCancelled {
		t.Errorf("任务状态应该是Cancelled，实际: %s", taskStatus.Status)
	}
}

// 集成测试环境
type integrationEnvironment struct {
	loadMonitor     *MockLoadMonitor
	rebalancer      *Rebalancer
	migrator        *DataMigrator
	hashRing        *ConsistentHashRing
	storageEngine   *MockStorageEngine
	networkTransfer *MockNetworkTransfer
}

// createIntegrationEnvironment 创建集成测试环境
func createIntegrationEnvironment() *integrationEnvironment {
	// 创建所有组件
	loadMonitor := NewMockLoadMonitor()
	hashRing := createTestHashRing()
	storageEngine := NewMockStorageEngine()
	networkTransfer := NewMockNetworkTransfer()

	// 创建重平衡调度器
	rebalancerConfig := DefaultRebalancerConfig()
	rebalancerConfig.CheckInterval = 50 * time.Millisecond
	rebalancer := NewRebalancer(rebalancerConfig, nil, hashRing, nil, loadMonitor)

	// 创建数据迁移执行器
	migratorConfig := DefaultDataMigratorConfig()
	migratorConfig.ProgressReportInterval = 25 * time.Millisecond
	migrator := NewDataMigrator(migratorConfig, storageEngine, networkTransfer, nil)

	return &integrationEnvironment{
		loadMonitor:     loadMonitor,
		rebalancer:      rebalancer,
		migrator:        migrator,
		hashRing:        hashRing,
		storageEngine:   storageEngine,
		networkTransfer: networkTransfer,
	}
}

// startAll 启动所有组件
func (env *integrationEnvironment) startAll(t *testing.T) {
	if err := env.rebalancer.Start(); err != nil {
		t.Fatalf("启动重平衡调度器失败: %v", err)
	}

	if err := env.migrator.Start(); err != nil {
		t.Fatalf("启动数据迁移执行器失败: %v", err)
	}
}

// cleanup 清理环境
func (env *integrationEnvironment) cleanup() {
	if env.rebalancer != nil {
		env.rebalancer.Stop()
	}
	if env.migrator != nil {
		env.migrator.Stop()
	}
}

// simulateHighLoad 模拟高负载
func (env *integrationEnvironment) simulateHighLoad(nodeID string, loadScore float64) {
	env.loadMonitor.SetNodeMetrics(raft.NodeID(nodeID), &raft.LoadMetrics{
		LoadScore: loadScore,
	})
}

// populateTestData 填充测试数据
func (env *integrationEnvironment) populateTestData(count int) {
	for i := 0; i < count; i++ {
		key := fmt.Sprintf("test-key-%06d", i)
		value := fmt.Sprintf("test-value-%06d", i)
		env.storageEngine.Put([]byte(key), []byte(value))
	}
}

// createTestHashRing 创建测试哈希环
func createTestHashRing() *ConsistentHashRing {
	config := DefaultHashRingConfig()
	ring := NewConsistentHashRing(config)
	ring.AddNode("node-1", "addr1", 1.0)
	ring.AddNode("node-2", "addr2", 1.0)
	ring.AddNode("node-3", "addr3", 1.0)
	return ring
}
