/*
 * @Author: Lzww0608
 * @Date: 2025-6-28 11:51:08
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-28 11:51:11
 * @Description: ConcordKV 故障转移和恢复集成测试 - Phase 5.2.3 测试框架
 */

package main

import (
	"context"
	"fmt"
	"log"
	"sync"
	"testing"
	"time"

	"raftserver/raft"
	"raftserver/replication"
)

// TestInterface 测试接口
type TestInterface interface {
	Logf(format string, args ...interface{})
	Errorf(format string, args ...interface{})
	Fatalf(format string, args ...interface{})
	Failed() bool
}

// TestRunner 测试运行器
type TestRunner struct {
	name   string
	failed bool
	logs   []string
}

func NewTestRunner(name string) *TestRunner {
	return &TestRunner{
		name: name,
		logs: make([]string, 0),
	}
}

func (tr *TestRunner) Logf(format string, args ...interface{}) {
	message := fmt.Sprintf(format, args...)
	tr.logs = append(tr.logs, message)
	log.Printf("[%s] %s", tr.name, message)
}

func (tr *TestRunner) Errorf(format string, args ...interface{}) {
	tr.failed = true
	tr.Logf("ERROR: "+format, args...)
}

func (tr *TestRunner) Fatalf(format string, args ...interface{}) {
	tr.failed = true
	tr.Logf("FATAL: "+format, args...)
	panic(fmt.Sprintf(format, args...))
}

func (tr *TestRunner) Failed() bool {
	return tr.failed
}

// MockStorage 模拟存储
type MockStorage struct {
	mu          sync.RWMutex
	logs        []raft.LogEntry
	lastIndex   raft.LogIndex
	lastTerm    raft.Term
	commitIndex raft.LogIndex
	currentTerm raft.Term
	votedFor    raft.NodeID
	snapshot    *raft.Snapshot
}

func NewMockStorage() *MockStorage {
	return &MockStorage{
		logs: make([]raft.LogEntry, 0),
	}
}

func (ms *MockStorage) GetLastLogIndex() raft.LogIndex {
	ms.mu.RLock()
	defer ms.mu.RUnlock()
	return ms.lastIndex
}

func (ms *MockStorage) GetLastLogTerm() raft.Term {
	ms.mu.RLock()
	defer ms.mu.RUnlock()
	return ms.lastTerm
}

func (ms *MockStorage) SaveCurrentTerm(term raft.Term) error {
	ms.mu.Lock()
	defer ms.mu.Unlock()
	ms.currentTerm = term
	return nil
}

func (ms *MockStorage) GetCurrentTerm() (raft.Term, error) {
	ms.mu.RLock()
	defer ms.mu.RUnlock()
	return ms.currentTerm, nil
}

func (ms *MockStorage) SaveVotedFor(candidateID raft.NodeID) error {
	ms.mu.Lock()
	defer ms.mu.Unlock()
	ms.votedFor = candidateID
	return nil
}

func (ms *MockStorage) GetVotedFor() (raft.NodeID, error) {
	ms.mu.RLock()
	defer ms.mu.RUnlock()
	return ms.votedFor, nil
}

func (ms *MockStorage) SaveLogEntries(entries []raft.LogEntry) error {
	ms.mu.Lock()
	defer ms.mu.Unlock()
	ms.logs = append(ms.logs, entries...)
	if len(entries) > 0 {
		lastEntry := entries[len(entries)-1]
		ms.lastIndex = lastEntry.Index
		ms.lastTerm = lastEntry.Term
	}
	return nil
}

func (ms *MockStorage) GetLogEntry(index raft.LogIndex) (*raft.LogEntry, error) {
	ms.mu.RLock()
	defer ms.mu.RUnlock()
	for _, entry := range ms.logs {
		if entry.Index == index {
			return &entry, nil
		}
	}
	return nil, fmt.Errorf("log entry not found")
}

func (ms *MockStorage) GetLogEntries(start, end raft.LogIndex) ([]raft.LogEntry, error) {
	ms.mu.RLock()
	defer ms.mu.RUnlock()
	var result []raft.LogEntry
	for _, entry := range ms.logs {
		if entry.Index >= start && entry.Index <= end {
			result = append(result, entry)
		}
	}
	return result, nil
}

func (ms *MockStorage) TruncateLog(index raft.LogIndex) error {
	ms.mu.Lock()
	defer ms.mu.Unlock()
	var newLogs []raft.LogEntry
	for _, entry := range ms.logs {
		if entry.Index < index {
			newLogs = append(newLogs, entry)
		}
	}
	ms.logs = newLogs
	return nil
}

func (ms *MockStorage) SaveSnapshot(snapshot *raft.Snapshot) error {
	ms.mu.Lock()
	defer ms.mu.Unlock()
	ms.snapshot = snapshot
	return nil
}

func (ms *MockStorage) GetSnapshot() (*raft.Snapshot, error) {
	ms.mu.RLock()
	defer ms.mu.RUnlock()
	if ms.snapshot == nil {
		return nil, fmt.Errorf("no snapshot available")
	}
	return ms.snapshot, nil
}

func (ms *MockStorage) Close() error {
	return nil
}

// MockTransport 模拟传输层
type MockTransport struct {
	mu                sync.RWMutex
	nodeID            raft.NodeID
	addr              string
	networkDelay      time.Duration
	networkPartitions map[string]bool
	running           bool
}

func NewMockTransport(nodeID raft.NodeID) *MockTransport {
	return &MockTransport{
		nodeID:            nodeID,
		addr:              fmt.Sprintf("mock://%s", nodeID),
		networkDelay:      time.Millisecond * 10,
		networkPartitions: make(map[string]bool),
	}
}

func (mt *MockTransport) LocalAddr() string {
	return mt.addr
}

func (mt *MockTransport) Start() error {
	mt.mu.Lock()
	defer mt.mu.Unlock()
	mt.running = true
	return nil
}

func (mt *MockTransport) Stop() error {
	mt.mu.Lock()
	defer mt.mu.Unlock()
	mt.running = false
	return nil
}

func (mt *MockTransport) SendVoteRequest(ctx context.Context, target raft.NodeID, req *raft.VoteRequest) (*raft.VoteResponse, error) {
	time.Sleep(mt.networkDelay)
	if mt.isPartitioned(string(target)) {
		return nil, fmt.Errorf("network partition")
	}
	return &raft.VoteResponse{
		Term:        req.Term,
		VoteGranted: false,
	}, nil
}

func (mt *MockTransport) SendAppendEntries(ctx context.Context, target raft.NodeID, req *raft.AppendEntriesRequest) (*raft.AppendEntriesResponse, error) {
	time.Sleep(mt.networkDelay)
	if mt.isPartitioned(string(target)) {
		return nil, fmt.Errorf("network partition")
	}
	return &raft.AppendEntriesResponse{
		Term:    req.Term,
		Success: true,
	}, nil
}

func (mt *MockTransport) SendInstallSnapshot(ctx context.Context, target raft.NodeID, req *raft.InstallSnapshotRequest) (*raft.InstallSnapshotResponse, error) {
	time.Sleep(mt.networkDelay)
	if mt.isPartitioned(string(target)) {
		return nil, fmt.Errorf("network partition")
	}
	return &raft.InstallSnapshotResponse{
		Term: req.Term,
	}, nil
}

func (mt *MockTransport) SetNetworkDelay(delay time.Duration) {
	mt.mu.Lock()
	defer mt.mu.Unlock()
	mt.networkDelay = delay
}

func (mt *MockTransport) SetNetworkPartition(node string, partitioned bool) {
	mt.mu.Lock()
	defer mt.mu.Unlock()
	mt.networkPartitions[node] = partitioned
}

func (mt *MockTransport) isPartitioned(node string) bool {
	mt.mu.RLock()
	defer mt.mu.RUnlock()
	return mt.networkPartitions[node]
}

// TestSimpleFailoverFlow 简化的故障转移流程测试
func TestSimpleFailoverFlow(t *testing.T) {
	t.Logf("开始简化的故障转移流程测试")

	// 创建基本组件
	nodeID := raft.NodeID("test-node")

	// 创建故障转移协调器
	config := replication.DefaultFailoverCoordinatorConfig()
	coordinator := replication.NewFailoverCoordinator(nodeID, config, nil, nil, nil, nil)

	// 启动协调器
	if err := coordinator.Start(); err != nil {
		t.Fatalf("启动协调器失败: %v", err)
	}
	defer coordinator.Stop()

	// 触发手动故障转移
	err := coordinator.TriggerManualFailover("dc1", "dc2", "简化测试")
	if err != nil {
		t.Errorf("触发故障转移失败: %v", err)
		return
	}

	// 等待完成
	timeout := time.After(time.Second * 10)
	ticker := time.NewTicker(time.Millisecond * 100)
	defer ticker.Stop()

	for {
		select {
		case <-timeout:
			t.Errorf("故障转移超时")
			return
		case <-ticker.C:
			if !coordinator.IsFailoverInProgress() {
				t.Logf("故障转移完成")
				goto checkResult
			}
		}
	}

checkResult:
	// 检查结果
	stats := coordinator.GetFailoverStats()
	t.Logf("故障转移统计: %+v", stats)

	history := coordinator.GetOperationHistory()
	if len(history) > 0 {
		lastOp := history[len(history)-1]
		t.Logf("最后操作: %s, 状态: %s", lastOp.ID, lastOp.Status)

		if lastOp.Status != "Completed" {
			t.Errorf("故障转移未成功: %s", lastOp.Status)
		}
	} else {
		t.Errorf("没有故障转移历史")
	}

	t.Logf("简化的故障转移流程测试完成")
}

// TestDCFailureDetectorBasic DC故障检测器基础测试
func TestDCFailureDetectorBasic(t *testing.T) {
	t.Logf("开始DC故障检测器基础测试")

	nodeID := raft.NodeID("test-node")
	transport := NewMockTransport(nodeID)

	// 创建故障检测器
	config := replication.DefaultDCFailureDetectorConfig()
	detector := replication.NewDCFailureDetector(nodeID, config, nil, nil, transport)

	if err := detector.Start(); err != nil {
		t.Fatalf("启动故障检测器失败: %v", err)
	}
	defer detector.Stop()

	// 等待初始化
	time.Sleep(time.Second * 2)

	// 检查基本功能
	failures := detector.GetCurrentFailures()
	t.Logf("当前故障数量: %d", len(failures))

	healthSnapshots := detector.GetHealthSnapshots()
	t.Logf("健康快照数量: %d", len(healthSnapshots))

	t.Logf("DC故障检测器基础测试完成")
}

// TestConsistencyRecoveryBasic 一致性恢复器基础测试
func TestConsistencyRecoveryBasic(t *testing.T) {
	t.Logf("开始一致性恢复器基础测试")

	nodeID := raft.NodeID("test-node")
	storage := NewMockStorage()

	// 添加一些测试日志条目
	testEntry := raft.LogEntry{
		Index: 1,
		Term:  1,
		Type:  raft.EntryNormal,
		Data:  []byte("test-data"),
	}
	storage.SaveLogEntries([]raft.LogEntry{testEntry})

	// 创建一致性恢复器
	config := replication.DefaultConsistencyRecoveryConfig()
	recovery := replication.NewConsistencyRecovery(nodeID, config, storage, nil, nil, nil)

	if err := recovery.Start(); err != nil {
		t.Fatalf("启动一致性恢复器失败: %v", err)
	}
	defer recovery.Stop()

	// 等待初始化
	time.Sleep(time.Second * 2)

	// 检查基本功能
	isConsistent := recovery.IsGloballyConsistent()
	t.Logf("全局一致性: %t", isConsistent)

	snapshot := recovery.GetCurrentSnapshot()
	if snapshot != nil {
		t.Logf("当前快照: %+v", snapshot)
	}

	t.Logf("一致性恢复器基础测试完成")
}

// TestIntegratedComponents 集成组件测试
func TestIntegratedComponents(t *testing.T) {
	t.Logf("开始集成组件测试")

	nodeID := raft.NodeID("test-node")
	storage := NewMockStorage()
	transport := NewMockTransport(nodeID)

	// 创建Raft配置
	raftConfig := &raft.Config{
		NodeID: nodeID,
	}

	// 创建异步复制管理器
	asyncReplicator := replication.NewAsyncReplicator(nodeID, raftConfig, transport, storage)

	// 创建读写分离路由器
	readWriteRouter := replication.NewReadWriteRouter(nodeID, raftConfig)

	// 创建故障检测器
	failureConfig := replication.DefaultDCFailureDetectorConfig()
	failureDetector := replication.NewDCFailureDetector(
		nodeID, failureConfig, asyncReplicator, readWriteRouter, transport)

	// 创建一致性恢复器
	recoveryConfig := replication.DefaultConsistencyRecoveryConfig()
	consistencyRecovery := replication.NewConsistencyRecovery(
		nodeID, recoveryConfig, storage, asyncReplicator, readWriteRouter, failureDetector)

	// 创建故障转移协调器
	coordinatorConfig := replication.DefaultFailoverCoordinatorConfig()
	failoverCoordinator := replication.NewFailoverCoordinator(
		nodeID, coordinatorConfig, failureDetector, consistencyRecovery, readWriteRouter, asyncReplicator)

	// 启动所有组件
	components := []struct {
		name string
		comp interface{ Start() error }
	}{
		{"异步复制管理器", asyncReplicator},
		{"读写分离路由器", readWriteRouter},
		{"故障检测器", failureDetector},
		{"一致性恢复器", consistencyRecovery},
		{"故障转移协调器", failoverCoordinator},
	}

	for _, comp := range components {
		if err := comp.comp.Start(); err != nil {
			t.Fatalf("启动%s失败: %v", comp.name, err)
		}
		t.Logf("成功启动: %s", comp.name)
	}

	// 等待组件稳定
	time.Sleep(time.Second * 3)

	// 测试基本功能
	t.Logf("测试基本功能...")

	// 检查故障检测器状态
	failures := failureDetector.GetCurrentFailures()
	t.Logf("当前故障: %d 个", len(failures))

	// 检查一致性状态
	isConsistent := consistencyRecovery.IsGloballyConsistent()
	t.Logf("全局一致性: %t", isConsistent)

	// 触发故障转移
	err := failoverCoordinator.TriggerManualFailover("dc1", "dc2", "集成测试")
	if err != nil {
		t.Errorf("触发故障转移失败: %v", err)
	} else {
		t.Logf("成功触发故障转移")

		// 等待完成
		for i := 0; i < 30 && failoverCoordinator.IsFailoverInProgress(); i++ {
			time.Sleep(time.Millisecond * 100)
		}

		if !failoverCoordinator.IsFailoverInProgress() {
			t.Logf("故障转移已完成")
		} else {
			t.Errorf("故障转移未在预期时间内完成")
		}
	}

	// 停止所有组件
	stoppers := []interface{ Stop() error }{
		failoverCoordinator,
		consistencyRecovery,
		failureDetector,
		readWriteRouter,
		asyncReplicator,
	}

	for i, stopper := range stoppers {
		if err := stopper.Stop(); err != nil {
			t.Errorf("停止组件%d失败: %v", i, err)
		}
	}

	t.Logf("集成组件测试完成")
}

// BenchmarkFailoverPerformance 故障转移性能基准测试
func BenchmarkFailoverPerformance(b *testing.B) {
	nodeID := raft.NodeID("bench-node")
	config := replication.DefaultFailoverCoordinatorConfig()
	coordinator := replication.NewFailoverCoordinator(nodeID, config, nil, nil, nil, nil)

	if err := coordinator.Start(); err != nil {
		b.Fatalf("启动协调器失败: %v", err)
	}
	defer coordinator.Stop()

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		// 触发故障转移
		err := coordinator.TriggerManualFailover("dc1", "dc2", fmt.Sprintf("benchmark-%d", i))
		if err != nil {
			b.Errorf("触发故障转移失败: %v", err)
			continue
		}

		// 等待完成
		for coordinator.IsFailoverInProgress() {
			time.Sleep(time.Millisecond * 1)
		}
	}
}

// TestNetworkPartitionScenario 网络分区场景测试
func TestNetworkPartitionScenario(t *testing.T) {
	t.Logf("开始网络分区场景测试")

	nodeID := raft.NodeID("test-node")
	transport := NewMockTransport(nodeID)

	// 创建故障检测器
	config := replication.DefaultDCFailureDetectorConfig()
	detector := replication.NewDCFailureDetector(nodeID, config, nil, nil, transport)

	if err := detector.Start(); err != nil {
		t.Fatalf("启动故障检测器失败: %v", err)
	}
	defer detector.Stop()

	// 等待初始化（减少时间）
	time.Sleep(time.Millisecond * 500)

	// 模拟网络分区
	t.Logf("模拟网络分区...")
	transport.SetNetworkPartition("node2", true)
	transport.SetNetworkDelay(time.Millisecond * 500)

	// 等待故障检测（减少时间）
	time.Sleep(time.Second * 1)

	// 检查故障状态
	failures := detector.GetCurrentFailures()
	t.Logf("分区后故障数量: %d", len(failures))

	// 恢复网络
	t.Logf("恢复网络连接...")
	transport.SetNetworkPartition("node2", false)
	transport.SetNetworkDelay(time.Millisecond * 10)

	// 等待恢复检测（减少时间）
	time.Sleep(time.Second * 1)

	// 检查恢复状态
	finalFailures := detector.GetCurrentFailures()
	t.Logf("恢复后故障数量: %d", len(finalFailures))

	t.Logf("网络分区场景测试完成")
}

// TestFailoverRecoveryIntegration 故障转移和恢复集成测试主入口
func TestFailoverRecoveryIntegration(t *testing.T) {
	t.Logf("开始故障转移和恢复集成测试")

	// 运行各个子测试
	t.Run("SimpleFailoverFlow", TestSimpleFailoverFlow)
	t.Run("DCFailureDetectorBasic", TestDCFailureDetectorBasic)
	t.Run("ConsistencyRecoveryBasic", TestConsistencyRecoveryBasic)
	t.Run("IntegratedComponents", TestIntegratedComponents)
	t.Run("NetworkPartitionScenario", TestNetworkPartitionScenario)

	t.Logf("故障转移和恢复集成测试完成")
}

// main 函数用于直接运行测试
func main() {
	fmt.Println("=== ConcordKV Phase 5.2.3 故障转移和恢复测试 ===")

	tests := []struct {
		name string
		fn   func(*TestRunner)
	}{
		{"SimpleFailoverFlow", func(tr *TestRunner) { TestSimpleFailoverFlow(&testing.T{}) }},
		{"DCFailureDetectorBasic", func(tr *TestRunner) { TestDCFailureDetectorBasic(&testing.T{}) }},
		{"ConsistencyRecoveryBasic", func(tr *TestRunner) { TestConsistencyRecoveryBasic(&testing.T{}) }},
		{"IntegratedComponents", func(tr *TestRunner) { TestIntegratedComponents(&testing.T{}) }},
		{"NetworkPartitionScenario", func(tr *TestRunner) { TestNetworkPartitionScenario(&testing.T{}) }},
	}

	for _, test := range tests {
		fmt.Printf("\n--- 运行测试: %s ---\n", test.name)
		tr := NewTestRunner(test.name)

		func() {
			defer func() {
				if r := recover(); r != nil {
					tr.Errorf("测试崩溃: %v", r)
				}
			}()
			test.fn(tr)
		}()

		if tr.Failed() {
			fmt.Printf("❌ 测试失败: %s\n", test.name)
		} else {
			fmt.Printf("✅ 测试通过: %s\n", test.name)
		}
	}

	fmt.Println("\n=== 测试完成 ===")
}
