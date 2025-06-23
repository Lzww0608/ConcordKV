package monitoring

import (
	"testing"
	"time"
)

// TestRaftIntegration 测试与Raft节点的集成
func TestRaftIntegration(t *testing.T) {
	node := NewMockNode("integration-test-node")
	config := DefaultLoadMonitorConfig()
	config.CollectionInterval = 50 * time.Millisecond
	monitor := NewLoadMonitor(config, node)

	err := monitor.Start()
	if err != nil {
		t.Fatalf("启动集成测试失败: %v", err)
	}
	defer monitor.Stop()

	// 模拟负载
	for i := 0; i < 10; i++ {
		monitor.RecordKeyAccess("integration-key")
		monitor.RecordOperation()
	}

	time.Sleep(100 * time.Millisecond)

	// 验证指标收集
	currentMetrics := monitor.GetCurrentMetrics()
	if currentMetrics == nil {
		t.Fatal("无法获取当前负载指标")
	}

	if currentMetrics.QPS <= 0 {
		t.Errorf("QPS应该大于0，实际: %.2f", currentMetrics.QPS)
	}

	snapshots := monitor.GetHistoricalSnapshots(1 * time.Minute)
	if len(snapshots) == 0 {
		t.Error("应该有历史快照数据")
	}
}

// TestConcurrentAccess 测试并发访问安全性
func TestConcurrentAccess(t *testing.T) {
	node := NewMockNode("concurrent-test")
	config := DefaultLoadMonitorConfig()
	monitor := NewLoadMonitor(config, node)

	monitor.Start()
	defer monitor.Stop()

	done := make(chan bool, 10)

	for i := 0; i < 10; i++ {
		go func(id int) {
			for j := 0; j < 10; j++ {
				monitor.RecordKeyAccess("concurrent-key")
				monitor.RecordOperation()
			}
			done <- true
		}(i)
	}

	for i := 0; i < 10; i++ {
		<-done
	}

	if monitor.totalOperations != 100 {
		t.Errorf("操作计数不正确，期望: 100, 实际: %d", monitor.totalOperations)
	}
}
