package sharding

import (
	"testing"
	"time"
	"raftserver/raft"
)

// MockLoadMonitor 模拟负载监控器
type MockLoadMonitor struct {
	nodeMetrics map[raft.NodeID]*raft.LoadMetrics
}

func NewMockLoadMonitor() *MockLoadMonitor {
	return &MockLoadMonitor{
		nodeMetrics: make(map[raft.NodeID]*raft.LoadMetrics),
	}
}

func (m *MockLoadMonitor) GetAllNodeMetrics() map[raft.NodeID]*raft.LoadMetrics {
	return m.nodeMetrics
}

func (m *MockLoadMonitor) GetNodeMetrics(nodeID raft.NodeID) *raft.LoadMetrics {
	return m.nodeMetrics[nodeID]
}

func (m *MockLoadMonitor) SetNodeMetrics(nodeID raft.NodeID, metrics *raft.LoadMetrics) {
	m.nodeMetrics[nodeID] = metrics
}

// TestRebalancerCreation 测试重平衡调度器创建
func TestRebalancerCreation(t *testing.T) {
	config := DefaultRebalancerConfig()
	loadMonitor := NewMockLoadMonitor()
	
	rebalancer := NewRebalancer(config, nil, nil, nil, loadMonitor)
	
	if rebalancer == nil {
		t.Fatal("重平衡调度器创建失败")
	}
	
	if rebalancer.config.LoadImbalanceThreshold != 0.2 {
		t.Errorf("负载不平衡阈值不正确，期望: 0.2, 实际: %.2f", rebalancer.config.LoadImbalanceThreshold)
	}
}

// TestLoadImbalanceDetection 测试负载不平衡检测
func TestLoadImbalanceDetection(t *testing.T) {
	config := DefaultRebalancerConfig()
	loadMonitor := NewMockLoadMonitor()
	rebalancer := NewRebalancer(config, nil, nil, nil, loadMonitor)
	
	// 设置模拟负载数据 - 平衡状态
	loadMonitor.SetNodeMetrics("node-1", &raft.LoadMetrics{LoadScore: 0.3})
	loadMonitor.SetNodeMetrics("node-2", &raft.LoadMetrics{LoadScore: 0.35})
	loadMonitor.SetNodeMetrics("node-3", &raft.LoadMetrics{LoadScore: 0.32})
	
	info := rebalancer.detectLoadImbalance()
	if info == nil {
		t.Fatal("应该能够检测负载状态")
	}
	
	if info.RequiresRebalance {
		t.Error("平衡状态不应该需要重平衡")
	}
	
	// 设置不平衡状态
	loadMonitor.SetNodeMetrics("node-1", &raft.LoadMetrics{LoadScore: 0.8}) // 高负载
	loadMonitor.SetNodeMetrics("node-2", &raft.LoadMetrics{LoadScore: 0.1}) // 低负载
	loadMonitor.SetNodeMetrics("node-3", &raft.LoadMetrics{LoadScore: 0.3})
	
	info = rebalancer.detectLoadImbalance()
	if info == nil {
		t.Fatal("应该能够检测不平衡状态")
	}
	
	if !info.RequiresRebalance {
		t.Error("不平衡状态应该需要重平衡")
	}
	
	if len(info.OverloadedNodes) == 0 {
		t.Error("应该检测到过载节点")
	}
	
	if len(info.UnderloadedNodes) == 0 {
		t.Error("应该检测到负载不足节点")
	}
}

// TestStartStop 测试启动和停止
func TestStartStop(t *testing.T) {
	config := DefaultRebalancerConfig()
	config.CheckInterval = 100 * time.Millisecond
	config.EnableAutoRebalance = false // 禁用自动重平衡避免测试干扰
	
	loadMonitor := NewMockLoadMonitor()
	rebalancer := NewRebalancer(config, nil, nil, nil, loadMonitor)
	
	err := rebalancer.Start()
	if err != nil {
		t.Fatalf("启动重平衡调度器失败: %v", err)
	}
	
	time.Sleep(50 * time.Millisecond)
	
	err = rebalancer.Stop()
	if err != nil {
		t.Errorf("停止重平衡调度器失败: %v", err)
	}
}

// TestWeightBasedRebalance 测试基于权重的重平衡
func TestWeightBasedRebalance(t *testing.T) {
	config := DefaultRebalancerConfig()
	config.RebalanceStrategy = StrategyWeightBased
	
	loadMonitor := NewMockLoadMonitor()
	hashRing := NewConsistentHashRing(DefaultHashRingConfig())
	
	// 添加测试节点
	hashRing.AddNode("node-1", "addr1", 1.0)
	hashRing.AddNode("node-2", "addr2", 1.0)
	
	rebalancer := NewRebalancer(config, nil, hashRing, nil, loadMonitor)
	
	// 设置不平衡负载
	loadMonitor.SetNodeMetrics("node-1", &raft.LoadMetrics{LoadScore: 0.8})
	loadMonitor.SetNodeMetrics("node-2", &raft.LoadMetrics{LoadScore: 0.2})
	
	info := &LoadImbalanceInfo{
		OverloadedNodes:   []raft.NodeID{"node-1"},
		UnderloadedNodes:  []raft.NodeID{"node-2"},
		RequiresRebalance: true,
	}
	
	// 执行基于权重的重平衡
	rebalancer.executeWeightBasedRebalance(info)
	
	// 验证权重是否被调整
	nodes := hashRing.GetPhysicalNodes()
	node1 := nodes["node-1"]
	if node1 == nil {
		t.Fatal("node-1 不存在")
	}
	
	// 高负载节点的权重应该被降低
	if node1.Weight >= 1.0 {
		t.Errorf("高负载节点的权重应该被降低，实际权重: %.3f", node1.Weight)
	}
}
