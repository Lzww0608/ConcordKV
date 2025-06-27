package main

import (
	"raftserver/raft"
	"testing"
	"time"
)

// TestDCRaftExtension 测试DC感知Raft扩展
func TestDCRaftExtension(t *testing.T) {
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
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled:            true,
			DCPriorityElection: true,
			MaxCrossDCLatency:  time.Second * 2,
			LocalDataCenter: &raft.DataCenterConfig{
				ID:                    raft.DataCenterID("dc1"),
				IsPrimary:             true,
				MaxAsyncBatchSize:     100,
				AsyncReplicationDelay: time.Millisecond * 100,
				EnableCompression:     true,
			},
			DataCenters: map[raft.DataCenterID]*raft.DataCenterConfig{
				raft.DataCenterID("dc1"): {
					ID:                    raft.DataCenterID("dc1"),
					IsPrimary:             true,
					MaxAsyncBatchSize:     100,
					AsyncReplicationDelay: time.Millisecond * 100,
					EnableCompression:     true,
				},
				raft.DataCenterID("dc2"): {
					ID:                    raft.DataCenterID("dc2"),
					IsPrimary:             false,
					MaxAsyncBatchSize:     50,
					AsyncReplicationDelay: time.Millisecond * 200,
					EnableCompression:     false,
				},
			},
		},
	}

	// 创建DC扩展
	dcExtension := raft.NewDCRaftExtension(config, raft.NodeID("node1"))

	if dcExtension == nil {
		t.Fatal("DC扩展创建失败")
	}

	// 测试应该开始选举（主DC）
	if !dcExtension.ShouldStartElection() {
		t.Error("主DC应该允许开始选举")
	}

	// 启动扩展
	if err := dcExtension.Start(); err != nil {
		t.Fatalf("启动DC扩展失败: %v", err)
	}

	// 停止扩展
	if err := dcExtension.Stop(); err != nil {
		t.Fatalf("停止DC扩展失败: %v", err)
	}
}

// TestDCPriorityElection 测试DC优先级选举
func TestDCPriorityElection(t *testing.T) {
	// 测试主数据中心优先级选举
	primaryDCConfig := &raft.Config{
		NodeID:            raft.NodeID("primary1"),
		ElectionTimeout:   time.Second * 5,
		HeartbeatInterval: time.Second * 1,
		Servers: []raft.Server{
			{
				ID:          raft.NodeID("primary1"),
				Address:     "localhost:8001",
				DataCenter:  raft.DataCenterID("dc1"),
				ReplicaType: raft.PrimaryReplica,
			},
			{
				ID:          raft.NodeID("secondary1"),
				Address:     "localhost:8002",
				DataCenter:  raft.DataCenterID("dc2"),
				ReplicaType: raft.AsyncReplica,
			},
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled:            true,
			DCPriorityElection: true,
			MaxCrossDCLatency:  time.Second * 2,
			LocalDataCenter: &raft.DataCenterConfig{
				ID:        raft.DataCenterID("dc1"),
				IsPrimary: true,
			},
		},
	}

	primaryDCExtension := raft.NewDCRaftExtension(primaryDCConfig, raft.NodeID("primary1"))
	if !primaryDCExtension.ShouldStartElection() {
		t.Error("主DC节点应该允许开始选举")
	}

	// 测试辅助数据中心选举限制
	secondaryDCConfig := &raft.Config{
		NodeID:            raft.NodeID("secondary1"),
		ElectionTimeout:   time.Second * 5,
		HeartbeatInterval: time.Second * 1,
		Servers: []raft.Server{
			{
				ID:          raft.NodeID("primary1"),
				Address:     "localhost:8001",
				DataCenter:  raft.DataCenterID("dc1"),
				ReplicaType: raft.PrimaryReplica,
			},
			{
				ID:          raft.NodeID("secondary1"),
				Address:     "localhost:8002",
				DataCenter:  raft.DataCenterID("dc2"),
				ReplicaType: raft.AsyncReplica,
			},
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled:            true,
			DCPriorityElection: true,
			MaxCrossDCLatency:  time.Second * 2,
			LocalDataCenter: &raft.DataCenterConfig{
				ID:        raft.DataCenterID("dc2"),
				IsPrimary: false,
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
			},
		},
	}

	secondaryDCExtension := raft.NewDCRaftExtension(secondaryDCConfig, raft.NodeID("secondary1"))

	// 模拟从主DC收到心跳，设置LastPrimaryDCHeartbeat为当前时间
	appendEntriesReq := &raft.AppendEntriesRequest{
		Term:         1,
		LeaderID:     raft.NodeID("primary1"),
		PrevLogIndex: 0,
		PrevLogTerm:  0,
		Entries:      []raft.LogEntry{},
		LeaderCommit: 0,
	}

	// 处理来自主DC的心跳
	secondaryDCExtension.ProcessAppendEntries(appendEntriesReq)

	// 现在辅助DC应该不允许开始选举，因为刚刚收到了主DC的心跳
	if secondaryDCExtension.ShouldStartElection() {
		t.Error("辅助DC在主DC正常时不应该开始选举")
	}
}

// TestCrossDCLatencyMonitoring 测试跨DC延迟监控
func TestCrossDCLatencyMonitoring(t *testing.T) {
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
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled:           true,
			MaxCrossDCLatency: time.Second * 2,
			LocalDataCenter: &raft.DataCenterConfig{
				ID:        raft.DataCenterID("dc1"),
				IsPrimary: true,
			},
		},
	}

	dcExtension := raft.NewDCRaftExtension(config, raft.NodeID("node1"))

	// 启动扩展
	if err := dcExtension.Start(); err != nil {
		t.Fatalf("启动DC扩展失败: %v", err)
	}
	defer dcExtension.Stop()

	// 获取延迟统计
	latencyStats := dcExtension.GetLatencyStats()
	if latencyStats == nil {
		t.Error("延迟统计不应该为空")
	}

	// 获取DC选举状态
	electionState := dcExtension.GetDCElectionState()
	if electionState == nil {
		t.Error("DC选举状态不应该为空")
	}
}

// TestAsyncReplication 测试异步复制
func TestAsyncReplication(t *testing.T) {
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
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled: true,
			LocalDataCenter: &raft.DataCenterConfig{
				ID:                    raft.DataCenterID("dc1"),
				IsPrimary:             true,
				MaxAsyncBatchSize:     10,
				AsyncReplicationDelay: time.Millisecond * 100,
				EnableCompression:     true,
			},
		},
	}

	dcExtension := raft.NewDCRaftExtension(config, raft.NodeID("node1"))

	// 启动扩展
	if err := dcExtension.Start(); err != nil {
		t.Fatalf("启动DC扩展失败: %v", err)
	}
	defer dcExtension.Stop()

	// 测试添加异步复制条目
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

	dcExtension.AddToAsyncReplication(entries)

	// 等待一段时间让异步复制处理
	time.Sleep(time.Millisecond * 200)
}

// TestDCHealthChecker 测试DC健康检查器
func TestDCHealthChecker(t *testing.T) {
	// 这个测试需要创建一个模拟的健康检查器
	// 由于DCHealthChecker是Node的内部结构，我们测试其功能

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
		},
		MultiDC: &raft.MultiDCConfig{
			Enabled: true,
			LocalDataCenter: &raft.DataCenterConfig{
				ID:        raft.DataCenterID("dc1"),
				IsPrimary: true,
			},
		},
	}

	dcExtension := raft.NewDCRaftExtension(config, raft.NodeID("node1"))

	if dcExtension == nil {
		t.Fatal("DC扩展创建失败")
	}

	// 启动扩展以初始化健康检查
	if err := dcExtension.Start(); err != nil {
		t.Fatalf("启动DC扩展失败: %v", err)
	}
	defer dcExtension.Stop()

	// 等待一段时间让健康检查初始化
	time.Sleep(time.Millisecond * 100)
}
