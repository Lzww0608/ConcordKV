/*
* @Author: Lzww0608
* @Date: 2025-6-23 17:10:29
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-23 17:10:29
* @Description: ConcordKV 负载监控代理单元测试
 */

package monitoring

import (
	"testing"
	"time"

	"raftserver/raft"
)

// MockNode 模拟Raft节点 - 用于测试
type MockNode struct {
	id      raft.NodeID
	metrics *raft.Metrics
}

func NewMockNode(id raft.NodeID) *MockNode {
	return &MockNode{
		id: id,
		metrics: &raft.Metrics{
			CurrentTerm:  1,
			State:        raft.Follower,
			Leader:       "leader-1",
			LastLogIndex: 100,
			CommitIndex:  90,
			LastApplied:  85,
			CollectedAt:  time.Now(),
		},
	}
}

func (m *MockNode) GetMetrics() *raft.Metrics {
	return m.metrics
}

func (m *MockNode) GetID() raft.NodeID {
	return m.id
}

func (m *MockNode) AddEventListener(listener raft.EventListener) {
	// Mock implementation
}

// TestLoadMonitorCreation 测试负载监控代理创建
func TestLoadMonitorCreation(t *testing.T) {
	node := NewMockNode("test-node-1")
	config := DefaultLoadMonitorConfig()

	monitor := NewLoadMonitor(config, node)

	if monitor == nil {
		t.Fatal("负载监控代理创建失败")
	}

	if monitor.node.GetID() != "test-node-1" {
		t.Errorf("节点ID不匹配，期望: test-node-1, 实际: %s", monitor.node.GetID())
	}
}

// TestKeyAccessRecording 测试键访问记录
func TestKeyAccessRecording(t *testing.T) {
	node := NewMockNode("test-node-1")
	config := DefaultLoadMonitorConfig()
	monitor := NewLoadMonitor(config, node)

	// 记录键访问
	monitor.RecordKeyAccess("test-key-1")
	monitor.RecordKeyAccess("test-key-1")

	// 检查记录是否正确
	monitor.mu.RLock()
	record, exists := monitor.keyAccess["test-key-1"]
	monitor.mu.RUnlock()

	if !exists {
		t.Error("test-key-1 访问记录不存在")
	}

	if record.Count != 2 {
		t.Errorf("test-key-1 访问次数不正确，期望: 2, 实际: %d", record.Count)
	}
}

// TestLoadScoreCalculation 测试负载评分计算
func TestLoadScoreCalculation(t *testing.T) {
	node := NewMockNode("test-node-1")
	config := DefaultLoadMonitorConfig()
	monitor := NewLoadMonitor(config, node)

	// 创建测试指标
	metrics := &raft.LoadMetrics{
		QPS:            500.0,
		StorageUsage:   1024.0,
		MemoryUsage:    0.5,
		CPUUsage:       0.3,
		NetworkIOBytes: 10 * 1024 * 1024,
	}

	score := monitor.calculateLoadScore(metrics)

	// 负载评分应该在0-1之间
	if score < 0.0 || score > 1.0 {
		t.Errorf("负载评分超出范围，期望: 0.0-1.0, 实际: %.3f", score)
	}
}
