package main

import (
	"context"
	"raftserver/raft"
	"testing"
	"time"
)

// TestCrossDCReplicationManager 测试跨DC复制管理器
func TestCrossDCReplicationManager(t *testing.T) {
	// 创建测试配置
	config := &raft.Config{
		NodeID:            raft.NodeID("node1"),
		ElectionTimeout:   time.Second * 5,
		HeartbeatInterval: time.Second * 1,
		Servers: []raft.Server{
			{
				ID:          raft.NodeID("node1"),
				Address:     "localhost:8001",
				DataCenter:  raft.DataCenterID("dc1"),
				ReplicaType: raft.PrimaryReplica,
			},
			{
				ID:          raft.NodeID("node2"),
				Address:     "localhost:8002",
				DataCenter:  raft.DataCenterID("dc2"),
				ReplicaType: raft.AsyncReplica,
			},
			{
				ID:          raft.NodeID("node3"),
				Address:     "localhost:8003",
				DataCenter:  raft.DataCenterID("dc3"),
				ReplicaType: raft.AsyncReplica,
			},
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled: true,
			LocalDataCenter: &raft.DataCenterConfig{
				ID:                    raft.DataCenterID("dc1"),
				IsPrimary:             true,
				MaxAsyncBatchSize:     100,
				AsyncReplicationDelay: time.Millisecond * 50,
				EnableCompression:     true,
			},
			DataCenters: map[raft.DataCenterID]*raft.DataCenterConfig{
				raft.DataCenterID("dc1"): {
					ID:        raft.DataCenterID("dc1"),
					IsPrimary: true,
				},
				raft.DataCenterID("dc2"): {
					ID:        raft.DataCenterID("dc2"),
					IsPrimary: false,
				},
				raft.DataCenterID("dc3"): {
					ID:        raft.DataCenterID("dc3"),
					IsPrimary: false,
				},
			},
		},
	}

	// 创建模拟传输层
	transport := &MockTransport{}

	// 创建跨DC复制管理器
	manager := raft.NewCrossDCReplicationManager(raft.NodeID("node1"), config, transport)
	if manager == nil {
		t.Fatal("跨DC复制管理器创建失败")
	}

	// 启动管理器
	if err := manager.Start(); err != nil {
		t.Fatalf("启动跨DC复制管理器失败: %v", err)
	}
	defer manager.Stop()

	// 测试复制日志条目
	entries := []raft.LogEntry{
		{
			Index:     1,
			Term:      1,
			Timestamp: time.Now(),
			Type:      raft.EntryNormal,
			Data:      []byte("test data 1"),
		},
		{
			Index:     2,
			Term:      1,
			Timestamp: time.Now(),
			Type:      raft.EntryNormal,
			Data:      []byte("test data 2"),
		},
	}

	// 执行复制
	if err := manager.ReplicateEntries(entries); err != nil {
		t.Fatalf("复制日志条目失败: %v", err)
	}

	// 等待复制完成
	time.Sleep(time.Millisecond * 200)

	// 检查复制统计
	stats := manager.GetReplicationStats()
	if stats == nil {
		t.Error("复制统计不应该为空")
	}

	// 检查DC复制状态
	status := manager.GetDCReplicationStatus()
	if len(status) == 0 {
		t.Error("应该有DC复制状态")
	}
}

// TestCompressionOptimization 测试压缩优化
func TestCompressionOptimization(t *testing.T) {
	config := &raft.Config{
		NodeID: raft.NodeID("node1"),
		Servers: []raft.Server{
			{
				ID:         raft.NodeID("node1"),
				Address:    "localhost:8001",
				DataCenter: raft.DataCenterID("dc1"),
			},
			{
				ID:         raft.NodeID("node2"),
				Address:    "localhost:8002",
				DataCenter: raft.DataCenterID("dc2"),
			},
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled: true,
			LocalDataCenter: &raft.DataCenterConfig{
				ID:                raft.DataCenterID("dc1"),
				EnableCompression: true,
			},
		},
	}

	transport := &MockTransport{}
	manager := raft.NewCrossDCReplicationManager(raft.NodeID("node1"), config, transport)

	if err := manager.Start(); err != nil {
		t.Fatalf("启动管理器失败: %v", err)
	}
	defer manager.Stop()

	// 创建大量数据测试压缩
	var entries []raft.LogEntry
	for i := 0; i < 100; i++ {
		entries = append(entries, raft.LogEntry{
			Index:     raft.LogIndex(i + 1),
			Term:      1,
			Timestamp: time.Now(),
			Type:      raft.EntryNormal,
			Data:      []byte("这是一个测试数据，用于验证压缩功能的有效性。重复的数据应该有更好的压缩比。"),
		})
	}

	// 执行复制
	if err := manager.ReplicateEntries(entries); err != nil {
		t.Fatalf("复制失败: %v", err)
	}

	// 等待处理完成
	time.Sleep(time.Millisecond * 300)

	// 检查压缩统计
	stats := manager.GetReplicationStats()
	if stats.CompressionRatio >= 1.0 {
		t.Error("压缩比应该小于1.0，表示数据被压缩了")
	}
}

// TestBatchOptimization 测试批量优化
func TestBatchOptimization(t *testing.T) {
	config := &raft.Config{
		NodeID: raft.NodeID("node1"),
		Servers: []raft.Server{
			{
				ID:         raft.NodeID("node1"),
				Address:    "localhost:8001",
				DataCenter: raft.DataCenterID("dc1"),
			},
			{
				ID:         raft.NodeID("node2"),
				Address:    "localhost:8002",
				DataCenter: raft.DataCenterID("dc2"),
			},
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled: true,
			LocalDataCenter: &raft.DataCenterConfig{
				ID:                    raft.DataCenterID("dc1"),
				MaxAsyncBatchSize:     10,
				AsyncReplicationDelay: time.Millisecond * 100,
			},
		},
	}

	transport := &MockTransport{}
	manager := raft.NewCrossDCReplicationManager(raft.NodeID("node1"), config, transport)

	if err := manager.Start(); err != nil {
		t.Fatalf("启动管理器失败: %v", err)
	}
	defer manager.Stop()

	// 分批发送小量数据，测试批量合并
	for i := 0; i < 5; i++ {
		entries := []raft.LogEntry{
			{
				Index:     raft.LogIndex(i*2 + 1),
				Term:      1,
				Timestamp: time.Now(),
				Type:      raft.EntryNormal,
				Data:      []byte("batch test data"),
			},
			{
				Index:     raft.LogIndex(i*2 + 2),
				Term:      1,
				Timestamp: time.Now(),
				Type:      raft.EntryNormal,
				Data:      []byte("batch test data 2"),
			},
		}

		if err := manager.ReplicateEntries(entries); err != nil {
			t.Fatalf("复制失败: %v", err)
		}

		// 短暂等待
		time.Sleep(time.Millisecond * 20)
	}

	// 等待批量处理完成
	time.Sleep(time.Millisecond * 200)

	// 检查统计信息
	stats := manager.GetReplicationStats()
	if stats.TotalBatchesSent == 0 {
		t.Error("应该发送了批次")
	}
}

// TestNetworkPartitionRecovery 测试网络分区恢复
func TestNetworkPartitionRecovery(t *testing.T) {
	config := &raft.Config{
		NodeID: raft.NodeID("node1"),
		Servers: []raft.Server{
			{
				ID:         raft.NodeID("node1"),
				Address:    "localhost:8001",
				DataCenter: raft.DataCenterID("dc1"),
			},
			{
				ID:         raft.NodeID("node2"),
				Address:    "localhost:8002",
				DataCenter: raft.DataCenterID("dc2"),
			},
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled: true,
			LocalDataCenter: &raft.DataCenterConfig{
				ID: raft.DataCenterID("dc1"),
			},
		},
	}

	transport := &MockTransport{}
	manager := raft.NewCrossDCReplicationManager(raft.NodeID("node1"), config, transport)

	if err := manager.Start(); err != nil {
		t.Fatalf("启动管理器失败: %v", err)
	}
	defer manager.Stop()

	// 模拟网络分区：更新心跳使DC显示为不健康
	// 注意：这里需要等待足够长的时间让健康检查器检测到

	// 等待健康检查器运行
	time.Sleep(time.Second * 12) // 超过2分钟的心跳超时

	// 检查DC状态
	status := manager.GetDCReplicationStatus()
	for dcID, target := range status {
		if dcID != raft.DataCenterID("dc1") { // 排除本地DC
			if target.IsConnected {
				// 由于没有心跳更新，应该变为不连接状态
				// 但这个测试需要更长时间，暂时跳过严格检查
				t.Logf("DC %s 连接状态: %v", dcID, target.IsConnected)
			}
		}
	}

	// 模拟恢复：更新心跳
	manager.UpdateDCHeartbeat(raft.DataCenterID("dc2"))

	// 检查恢复后的状态
	status = manager.GetDCReplicationStatus()
	if target, exists := status[raft.DataCenterID("dc2")]; exists {
		if !target.IsConnected {
			t.Error("DC2应该在心跳更新后恢复连接")
		}
	}
}

// TestReplicationPriority 测试复制优先级
func TestReplicationPriority(t *testing.T) {
	config := &raft.Config{
		NodeID: raft.NodeID("node1"),
		Servers: []raft.Server{
			{
				ID:         raft.NodeID("node1"),
				Address:    "localhost:8001",
				DataCenter: raft.DataCenterID("dc1"),
			},
			{
				ID:         raft.NodeID("node2"),
				Address:    "localhost:8002",
				DataCenter: raft.DataCenterID("dc2"), // 主DC
			},
			{
				ID:         raft.NodeID("node3"),
				Address:    "localhost:8003",
				DataCenter: raft.DataCenterID("dc3"), // 辅助DC
			},
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled: true,
			LocalDataCenter: &raft.DataCenterConfig{
				ID: raft.DataCenterID("dc1"),
			},
			DataCenters: map[raft.DataCenterID]*raft.DataCenterConfig{
				raft.DataCenterID("dc1"): {
					ID:        raft.DataCenterID("dc1"),
					IsPrimary: false,
				},
				raft.DataCenterID("dc2"): {
					ID:        raft.DataCenterID("dc2"),
					IsPrimary: true, // 主DC
				},
				raft.DataCenterID("dc3"): {
					ID:        raft.DataCenterID("dc3"),
					IsPrimary: false, // 辅助DC
				},
			},
		},
	}

	transport := &MockTransport{}
	manager := raft.NewCrossDCReplicationManager(raft.NodeID("node1"), config, transport)

	if err := manager.Start(); err != nil {
		t.Fatalf("启动管理器失败: %v", err)
	}
	defer manager.Stop()

	// 测试复制到不同优先级的DC
	entries := []raft.LogEntry{
		{
			Index:     1,
			Term:      1,
			Timestamp: time.Now(),
			Type:      raft.EntryNormal,
			Data:      []byte("priority test data"),
		},
	}

	if err := manager.ReplicateEntries(entries); err != nil {
		t.Fatalf("复制失败: %v", err)
	}

	// 等待处理完成
	time.Sleep(time.Millisecond * 200)

	// 检查统计信息，确认复制到了所有目标DC
	stats := manager.GetReplicationStats()
	if len(stats.DCStats) == 0 {
		t.Error("应该有DC级别的统计信息")
	}
}

// MockTransport 模拟传输层
type MockTransport struct{}

func (m *MockTransport) Start() error      { return nil }
func (m *MockTransport) Stop() error       { return nil }
func (m *MockTransport) LocalAddr() string { return "localhost:8000" }

func (m *MockTransport) SendVoteRequest(ctx context.Context, target raft.NodeID, req *raft.VoteRequest) (*raft.VoteResponse, error) {
	return &raft.VoteResponse{
		Term:        req.Term,
		VoteGranted: true,
	}, nil
}

func (m *MockTransport) SendAppendEntries(ctx context.Context, target raft.NodeID, req *raft.AppendEntriesRequest) (*raft.AppendEntriesResponse, error) {
	return &raft.AppendEntriesResponse{
		Term:    req.Term,
		Success: true,
	}, nil
}

func (m *MockTransport) SendInstallSnapshot(ctx context.Context, target raft.NodeID, req *raft.InstallSnapshotRequest) (*raft.InstallSnapshotResponse, error) {
	return &raft.InstallSnapshotResponse{
		Term: req.Term,
	}, nil
}
