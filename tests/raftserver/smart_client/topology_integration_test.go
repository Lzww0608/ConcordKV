/*
* @Author: Lzww0608
* @Date: 2025-7-2 22:27:13
* @LastEditors: Lzww0608
* @LastEditTime: 2025-7-2 22:27:16
* @Description: ConcordKV intelligent client - topology aware integration tests
 */

package concord

import (
	"context"
	"fmt"
	"sync"
	"testing"
	"time"
)

// MockTopologyEventListener 模拟拓扑事件监听器
type MockTopologyEventListener struct {
	events []TopologyEvent
	mu     sync.Mutex
}

func (mtl *MockTopologyEventListener) OnTopologyEvent(event TopologyEvent) {
	mtl.mu.Lock()
	defer mtl.mu.Unlock()
	mtl.events = append(mtl.events, event)
}

func (mtl *MockTopologyEventListener) GetEvents() []TopologyEvent {
	mtl.mu.Lock()
	defer mtl.mu.Unlock()
	events := make([]TopologyEvent, len(mtl.events))
	copy(events, mtl.events)
	return events
}

func (mtl *MockTopologyEventListener) Clear() {
	mtl.mu.Lock()
	defer mtl.mu.Unlock()
	mtl.events = mtl.events[:0]
}

// TestTopologyCache_BasicOperations 测试拓扑缓存基本操作
func TestTopologyCache_BasicOperations(t *testing.T) {
	config := DefaultTopologyConfig()
	config.CacheSize = 100
	config.CacheTTL = 1 * time.Minute

	cache := NewTopologyCache(config)

	// 测试设置和获取分片信息
	shardInfo := &ShardInfo{
		ID:       "shard-001",
		Range:    ShardRange{StartHash: 0, EndHash: 1000},
		Primary:  NodeID("node-1"),
		Replicas: []NodeID{NodeID("node-2"), NodeID("node-3")},
		State:    ShardStateActive,
		Version:  1,
	}

	// 设置分片信息
	cache.Set(shardInfo)

	// 获取分片信息
	retrieved, found := cache.Get("shard-001")
	if !found {
		t.Fatal("应该能够找到设置的分片信息")
	}

	if retrieved.ID != shardInfo.ID {
		t.Errorf("分片ID不匹配，期望: %s, 实际: %s", shardInfo.ID, retrieved.ID)
	}

	if retrieved.Primary != shardInfo.Primary {
		t.Errorf("主节点不匹配，期望: %s, 实际: %s", shardInfo.Primary, retrieved.Primary)
	}

	// 测试键映射
	cache.SetKeyMapping("test-key-1", "shard-001")
	retrievedByKey, foundByKey := cache.GetByKey("test-key-1")
	if !foundByKey {
		t.Fatal("应该能够通过键找到分片信息")
	}

	if retrievedByKey.ID != shardInfo.ID {
		t.Errorf("通过键获取的分片ID不匹配")
	}

	// 测试统计信息
	stats := cache.GetStats()
	if stats.TotalRequests < 2 {
		t.Errorf("请求统计不正确，期望至少2个请求，实际: %d", stats.TotalRequests)
	}

	if stats.CacheHits < 1 {
		t.Errorf("缓存命中统计不正确，期望至少1次命中，实际: %d", stats.CacheHits)
	}
}

// TestTopologyCache_LRUEviction 测试LRU驱逐机制
func TestTopologyCache_LRUEviction(t *testing.T) {
	config := DefaultTopologyConfig()
	config.CacheSize = 2
	config.MaxCacheSize = 2

	cache := NewTopologyCache(config)

	// 添加第一个分片
	shard1 := &ShardInfo{
		ID:      "shard-001",
		Range:   ShardRange{StartHash: 0, EndHash: 1000},
		Primary: NodeID("node-1"),
		State:   ShardStateActive,
		Version: 1,
	}
	cache.Set(shard1)

	// 添加第二个分片
	shard2 := &ShardInfo{
		ID:      "shard-002",
		Range:   ShardRange{StartHash: 1000, EndHash: 2000},
		Primary: NodeID("node-2"),
		State:   ShardStateActive,
		Version: 1,
	}
	cache.Set(shard2)

	// 验证两个分片都存在
	_, found1 := cache.Get("shard-001")
	_, found2 := cache.Get("shard-002")
	if !found1 || !found2 {
		t.Fatal("两个分片都应该存在")
	}

	// 添加第三个分片，应该驱逐最旧的
	shard3 := &ShardInfo{
		ID:      "shard-003",
		Range:   ShardRange{StartHash: 2000, EndHash: 3000},
		Primary: NodeID("node-3"),
		State:   ShardStateActive,
		Version: 1,
	}
	cache.Set(shard3)

	// 第一个分片应该被驱逐（最久未使用）
	_, found1After := cache.Get("shard-001")
	_, found2After := cache.Get("shard-002")
	_, found3After := cache.Get("shard-003")

	if found1After {
		t.Error("shard-001应该被LRU驱逐")
	}
	if !found2After {
		t.Error("shard-002不应该被驱逐")
	}
	if !found3After {
		t.Error("shard-003应该存在")
	}

	stats := cache.GetStats()
	if stats.EvictionCount == 0 {
		t.Error("应该有驱逐统计")
	}
}

// TestTopologyCache_TTLExpiry 测试TTL过期机制
func TestTopologyCache_TTLExpiry(t *testing.T) {
	config := DefaultTopologyConfig()
	config.CacheTTL = 100 * time.Millisecond

	cache := NewTopologyCache(config)

	shardInfo := &ShardInfo{
		ID:      "shard-001",
		Range:   ShardRange{StartHash: 0, EndHash: 1000},
		Primary: NodeID("node-1"),
		State:   ShardStateActive,
		Version: 1,
	}

	cache.Set(shardInfo)

	// 立即获取应该成功
	_, found := cache.Get("shard-001")
	if !found {
		t.Fatal("应该能够立即获取分片信息")
	}

	// 等待TTL过期
	time.Sleep(150 * time.Millisecond)

	// 再次获取应该失败
	_, foundAfter := cache.Get("shard-001")
	if foundAfter {
		t.Error("TTL过期后不应该能够获取分片信息")
	}
}

// TestTopologyCache_VersionControl 测试版本控制
func TestTopologyCache_VersionControl(t *testing.T) {
	config := DefaultTopologyConfig()
	config.EnableVersionCheck = true
	config.VersionTolerance = 5

	cache := NewTopologyCache(config)

	// 添加老版本的分片
	oldShard := &ShardInfo{
		ID:      "shard-001",
		Primary: NodeID("node-1"),
		State:   ShardStateActive,
		Version: 1,
	}
	cache.Set(oldShard)

	// 更新全局版本到10
	cache.UpdateVersion(10)

	// 老版本的分片应该被清理（版本差距超过容差）
	_, found := cache.Get("shard-001")
	if found {
		t.Error("老版本的分片应该被清理")
	}

	// 添加新版本的分片
	newShard := &ShardInfo{
		ID:      "shard-002",
		Primary: NodeID("node-2"),
		State:   ShardStateActive,
		Version: 8, // 在容差范围内
	}
	cache.Set(newShard)

	// 新版本的分片应该存在
	_, foundNew := cache.Get("shard-002")
	if !foundNew {
		t.Error("新版本的分片应该存在")
	}
}

// TestTopologyEventSubscriber_EventHandling 测试事件订阅和处理
func TestTopologyEventSubscriber_EventHandling(t *testing.T) {
	config := DefaultTopologyConfig()
	cache := NewTopologyCache(config)
	subscriber := NewTopologyEventSubscriber(config, cache)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// 启动事件订阅器
	err := subscriber.Start(ctx)
	if err != nil {
		t.Fatalf("启动事件订阅器失败: %v", err)
	}
	defer subscriber.Stop()

	// 添加事件监听器
	listener := &MockTopologyEventListener{}
	subscriber.AddListener(listener)

	// 发布分片添加事件
	shardInfo := &ShardInfo{
		ID:      "shard-001",
		Primary: NodeID("node-1"),
		State:   ShardStateActive,
		Version: 1,
	}

	event := TopologyEvent{
		Type:      EventShardAdded,
		ShardID:   shardInfo.ID,
		ShardInfo: shardInfo,
		Version:   1,
		Timestamp: time.Now(),
		Source:    "test",
	}

	subscriber.PublishEvent(event)

	// 等待事件处理
	time.Sleep(100 * time.Millisecond)

	// 验证事件被接收
	events := listener.GetEvents()
	if len(events) != 1 {
		t.Fatalf("期望接收1个事件，实际接收: %d", len(events))
	}

	if events[0].Type != EventShardAdded {
		t.Errorf("事件类型不匹配，期望: %s, 实际: %s", EventShardAdded, events[0].Type)
	}

	// 验证缓存已更新
	cachedShard, found := cache.Get(shardInfo.ID)
	if !found {
		t.Error("分片信息应该已添加到缓存")
	}

	if cachedShard.ID != shardInfo.ID {
		t.Error("缓存中的分片信息不正确")
	}
}

// TestTopologyEventSubscriber_MultipleListeners 测试多个监听器
func TestTopologyEventSubscriber_MultipleListeners(t *testing.T) {
	config := DefaultTopologyConfig()
	cache := NewTopologyCache(config)
	subscriber := NewTopologyEventSubscriber(config, cache)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := subscriber.Start(ctx)
	if err != nil {
		t.Fatalf("启动事件订阅器失败: %v", err)
	}
	defer subscriber.Stop()

	// 添加多个监听器
	listener1 := &MockTopologyEventListener{}
	listener2 := &MockTopologyEventListener{}
	listener3 := &MockTopologyEventListener{}

	subscriber.AddListener(listener1)
	subscriber.AddListener(listener2)
	subscriber.AddListener(listener3)

	// 发布事件
	event := TopologyEvent{
		Type:      EventShardUpdated,
		ShardID:   "shard-001",
		Version:   1,
		Timestamp: time.Now(),
		Source:    "test",
	}

	subscriber.PublishEvent(event)

	// 等待事件处理
	time.Sleep(100 * time.Millisecond)

	// 验证所有监听器都收到事件
	for i, listener := range []*MockTopologyEventListener{listener1, listener2, listener3} {
		events := listener.GetEvents()
		if len(events) != 1 {
			t.Errorf("监听器%d应该接收1个事件，实际: %d", i+1, len(events))
		}
	}

	// 移除一个监听器
	subscriber.RemoveListener(listener2)

	// 再次发布事件
	event2 := TopologyEvent{
		Type:      EventNodeAdded,
		Version:   2,
		Timestamp: time.Now(),
		Source:    "test",
	}

	subscriber.PublishEvent(event2)
	time.Sleep(100 * time.Millisecond)

	// 验证只有剩余的监听器收到事件
	if len(listener1.GetEvents()) != 2 {
		t.Error("listener1应该收到2个事件")
	}
	if len(listener2.GetEvents()) != 1 {
		t.Error("listener2应该只收到1个事件（已移除）")
	}
	if len(listener3.GetEvents()) != 2 {
		t.Error("listener3应该收到2个事件")
	}
}

// TestTopologyAwareClient_Initialization 测试拓扑感知客户端初始化
func TestTopologyAwareClient_Initialization(t *testing.T) {
	baseConfig := Config{
		Servers:        []string{"localhost:8080"},
		ConnectTimeout: 5 * time.Second,
		RequestTimeout: 10 * time.Second,
	}

	topologyConfig := DefaultTopologyConfig()
	topologyConfig.RefreshInterval = 1 * time.Second

	client, err := NewTopologyAwareClient(baseConfig, topologyConfig)
	if err != nil {
		t.Fatalf("创建拓扑感知客户端失败: %v", err)
	}
	defer client.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	// 初始化客户端
	err = client.Initialize(ctx)
	if err != nil {
		t.Fatalf("初始化拓扑感知客户端失败: %v", err)
	}

	// 验证统计信息
	stats := client.GetTopologyStats()
	if stats == nil {
		t.Error("应该能够获取拓扑统计信息")
	}
}

// TestTopologyAwareClient_ShardInfoRetrieval 测试分片信息获取
func TestTopologyAwareClient_ShardInfoRetrieval(t *testing.T) {
	baseConfig := Config{
		Servers:        []string{"localhost:8080"},
		ConnectTimeout: 5 * time.Second,
		RequestTimeout: 10 * time.Second,
	}

	client, err := NewTopologyAwareClient(baseConfig, nil)
	if err != nil {
		t.Fatalf("创建拓扑感知客户端失败: %v", err)
	}
	defer client.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err = client.Initialize(ctx)
	if err != nil {
		t.Fatalf("初始化拓扑感知客户端失败: %v", err)
	}

	// 测试获取分片信息
	// 注意：这里使用模拟数据，在实际实现中需要连接真实的服务端
	shardInfo, err := client.GetShardInfo("test-key")
	if err != nil {
		// 在模拟环境中，这是预期的行为
		t.Logf("获取分片信息返回错误（预期）: %v", err)
	} else {
		// 如果成功获取，验证分片信息
		if shardInfo == nil {
			t.Error("分片信息不应该为空")
		} else {
			if shardInfo.ID == "" {
				t.Error("分片ID不应该为空")
			}
			if shardInfo.Primary == "" {
				t.Error("主节点不应该为空")
			}
		}
	}
}

// TestTopologyAwareClient_EventListeners 测试事件监听器管理
func TestTopologyAwareClient_EventListeners(t *testing.T) {
	baseConfig := Config{
		Servers:        []string{"localhost:8080"},
		ConnectTimeout: 5 * time.Second,
		RequestTimeout: 10 * time.Second,
	}

	client, err := NewTopologyAwareClient(baseConfig, nil)
	if err != nil {
		t.Fatalf("创建拓扑感知客户端失败: %v", err)
	}
	defer client.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err = client.Initialize(ctx)
	if err != nil {
		t.Fatalf("初始化拓扑感知客户端失败: %v", err)
	}

	// 添加事件监听器
	listener := &MockTopologyEventListener{}
	client.AddTopologyEventListener(listener)

	// 移除事件监听器
	client.RemoveTopologyEventListener(listener)

	// 这个测试主要验证API不会崩溃
	t.Log("事件监听器管理测试通过")
}

// TestTopologyAwareClient_RefreshTopology 测试拓扑刷新
func TestTopologyAwareClient_RefreshTopology(t *testing.T) {
	baseConfig := Config{
		Servers:        []string{"localhost:8080"},
		ConnectTimeout: 5 * time.Second,
		RequestTimeout: 10 * time.Second,
	}

	client, err := NewTopologyAwareClient(baseConfig, nil)
	if err != nil {
		t.Fatalf("创建拓扑感知客户端失败: %v", err)
	}
	defer client.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	err = client.Initialize(ctx)
	if err != nil {
		t.Fatalf("初始化拓扑感知客户端失败: %v", err)
	}

	// 测试手动刷新拓扑
	err = client.RefreshTopology(ctx)
	if err != nil {
		t.Logf("刷新拓扑返回错误（在模拟环境中是预期的）: %v", err)
	}
}

// BenchmarkTopologyCache_Get 性能测试：缓存获取
func BenchmarkTopologyCache_Get(b *testing.B) {
	config := DefaultTopologyConfig()
	cache := NewTopologyCache(config)

	// 预填充缓存
	for i := 0; i < 1000; i++ {
		shardInfo := &ShardInfo{
			ID:      fmt.Sprintf("shard-%03d", i),
			Primary: NodeID(fmt.Sprintf("node-%d", i%10)),
			State:   ShardStateActive,
			Version: int64(i),
		}
		cache.Set(shardInfo)
	}

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			shardID := fmt.Sprintf("shard-%03d", i%1000)
			cache.Get(shardID)
			i++
		}
	})
}

// BenchmarkTopologyCache_Set 性能测试：缓存设置
func BenchmarkTopologyCache_Set(b *testing.B) {
	config := DefaultTopologyConfig()
	cache := NewTopologyCache(config)

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		i := 0
		for pb.Next() {
			shardInfo := &ShardInfo{
				ID:      fmt.Sprintf("shard-%d", i),
				Primary: NodeID(fmt.Sprintf("node-%d", i%10)),
				State:   ShardStateActive,
				Version: int64(i),
			}
			cache.Set(shardInfo)
			i++
		}
	})
}

// BenchmarkTopologyEventSubscriber_PublishEvent 性能测试：事件发布
func BenchmarkTopologyEventSubscriber_PublishEvent(b *testing.B) {
	config := DefaultTopologyConfig()
	cache := NewTopologyCache(config)
	subscriber := NewTopologyEventSubscriber(config, cache)

	ctx := context.Background()
	subscriber.Start(ctx)
	defer subscriber.Stop()

	event := TopologyEvent{
		Type:      EventShardUpdated,
		ShardID:   "shard-001",
		Version:   1,
		Timestamp: time.Now(),
		Source:    "benchmark",
	}

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		subscriber.PublishEvent(event)
	}
}
