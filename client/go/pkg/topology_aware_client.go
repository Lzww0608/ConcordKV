/*
* @Author: Lzww0608
* @Date: 22025-7-2 22:23:37
* @LastEditors: Lzww0608
* @LastEditTime: 2025-7-2 22:23:40
* @Description: ConcordKV intelligent client - topology aware module
 */

package concord

import (
	"context"
	"fmt"
	"sync"
	"sync/atomic"
	"time"
)

// NodeID 节点标识符 (复用raftserver类型)
type NodeID string

// ShardState 分片状态 (复用raftserver定义)
type ShardState int

const (
	ShardStateActive    ShardState = iota // 活跃状态，正常服务
	ShardStateMigrating                   // 迁移中，数据正在迁移
	ShardStateReadOnly                    // 只读状态，只能读取不能写入
	ShardStateOffline                     // 离线状态，不提供服务
)

func (s ShardState) String() string {
	switch s {
	case ShardStateActive:
		return "Active"
	case ShardStateMigrating:
		return "Migrating"
	case ShardStateReadOnly:
		return "ReadOnly"
	case ShardStateOffline:
		return "Offline"
	default:
		return "Unknown"
	}
}

// ShardRange 分片范围定义 (复用raftserver结构)
type ShardRange struct {
	StartHash uint64 `json:"startHash"` // 起始哈希值（包含）
	EndHash   uint64 `json:"endHash"`   // 结束哈希值（不包含）
}

// Contains 检查哈希值是否在分片范围内
func (sr *ShardRange) Contains(hash uint64) bool {
	if sr.StartHash <= sr.EndHash {
		// 正常范围
		return hash >= sr.StartHash && hash < sr.EndHash
	} else {
		// 跨越0点的环形范围
		return hash >= sr.StartHash || hash < sr.EndHash
	}
}

// Size 计算分片范围大小
func (sr *ShardRange) Size() uint64 {
	if sr.StartHash <= sr.EndHash {
		return sr.EndHash - sr.StartHash
	} else {
		return (^uint64(0) - sr.StartHash) + sr.EndHash + 1
	}
}

// ShardInfo 分片信息 (复用raftserver结构)
type ShardInfo struct {
	ID        string            `json:"id"`        // 分片ID
	Range     ShardRange        `json:"range"`     // 分片范围
	Primary   NodeID            `json:"primary"`   // 主节点
	Replicas  []NodeID          `json:"replicas"`  // 副本节点列表
	State     ShardState        `json:"state"`     // 分片状态
	Version   int64             `json:"version"`   // 版本号
	CreatedAt time.Time         `json:"createdAt"` // 创建时间
	UpdatedAt time.Time         `json:"updatedAt"` // 更新时间
	Metadata  map[string]string `json:"metadata"`  // 元数据
}

// TopologyConfig 拓扑感知配置
type TopologyConfig struct {
	// 拓扑缓存配置
	CacheSize    int           `json:"cacheSize"`    // 缓存大小
	CacheTTL     time.Duration `json:"cacheTTL"`     // 缓存TTL
	MaxCacheSize int           `json:"maxCacheSize"` // 最大缓存大小

	// 拓扑更新配置
	RefreshInterval time.Duration `json:"refreshInterval"` // 刷新间隔
	UpdateTimeout   time.Duration `json:"updateTimeout"`   // 更新超时
	MaxRetries      int           `json:"maxRetries"`      // 最大重试次数
	RetryInterval   time.Duration `json:"retryInterval"`   // 重试间隔

	// 事件订阅配置
	EnableEventStream  bool          `json:"enableEventStream"`  // 是否启用事件流
	EventStreamTimeout time.Duration `json:"eventStreamTimeout"` // 事件流超时
	ReconnectInterval  time.Duration `json:"reconnectInterval"`  // 重连间隔

	// 版本控制配置
	EnableVersionCheck bool `json:"enableVersionCheck"` // 是否启用版本检查
	VersionTolerance   int  `json:"versionTolerance"`   // 版本容差
}

// DefaultTopologyConfig 默认拓扑感知配置
func DefaultTopologyConfig() *TopologyConfig {
	return &TopologyConfig{
		CacheSize:          10000,
		CacheTTL:           5 * time.Minute,
		MaxCacheSize:       50000,
		RefreshInterval:    30 * time.Second,
		UpdateTimeout:      10 * time.Second,
		MaxRetries:         3,
		RetryInterval:      time.Second,
		EnableEventStream:  true,
		EventStreamTimeout: 60 * time.Second,
		ReconnectInterval:  5 * time.Second,
		EnableVersionCheck: true,
		VersionTolerance:   10,
	}
}

// TopologyCacheEntry 拓扑缓存条目
type TopologyCacheEntry struct {
	ShardInfo   *ShardInfo `json:"shardInfo"`   // 分片信息
	Timestamp   time.Time  `json:"timestamp"`   // 缓存时间
	Version     int64      `json:"version"`     // 版本号
	AccessCount int64      `json:"accessCount"` // 访问次数
}

// TopologyCache 拓扑缓存管理器
type TopologyCache struct {
	mu         sync.RWMutex
	config     *TopologyConfig
	entries    map[string]*TopologyCacheEntry // 分片ID -> 缓存条目
	keyToShard map[string]string              // 键 -> 分片ID的快速查找表
	version    int64                          // 全局版本号
	stats      *TopologyCacheStats            // 统计信息

	// LRU管理
	lruList    *cacheNode            // LRU双链表头
	lruTail    *cacheNode            // LRU双链表尾
	entryNodes map[string]*cacheNode // 分片ID -> LRU节点映射
}

// cacheNode LRU双链表节点
type cacheNode struct {
	shardID string
	prev    *cacheNode
	next    *cacheNode
}

// TopologyCacheStats 拓扑缓存统计信息
type TopologyCacheStats struct {
	TotalRequests  int64     `json:"totalRequests"`  // 总请求数
	CacheHits      int64     `json:"cacheHits"`      // 缓存命中数
	CacheMisses    int64     `json:"cacheMisses"`    // 缓存未命中数
	EvictionCount  int64     `json:"evictionCount"`  // 驱逐次数
	CurrentSize    int       `json:"currentSize"`    // 当前缓存大小
	LastUpdate     time.Time `json:"lastUpdate"`     // 最后更新时间
	HitRatio       float64   `json:"hitRatio"`       // 命中率
	AverageLatency float64   `json:"averageLatency"` // 平均延迟(毫秒)
}

// NewTopologyCache 创建新的拓扑缓存
func NewTopologyCache(config *TopologyConfig) *TopologyCache {
	if config == nil {
		config = DefaultTopologyConfig()
	}

	cache := &TopologyCache{
		config:     config,
		entries:    make(map[string]*TopologyCacheEntry),
		keyToShard: make(map[string]string),
		version:    1,
		stats:      &TopologyCacheStats{},
		entryNodes: make(map[string]*cacheNode),
	}

	// 初始化LRU双链表
	cache.lruList = &cacheNode{}
	cache.lruTail = &cacheNode{}
	cache.lruList.next = cache.lruTail
	cache.lruTail.prev = cache.lruList

	return cache
}

// Get 从缓存获取分片信息
func (tc *TopologyCache) Get(shardID string) (*ShardInfo, bool) {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	atomic.AddInt64(&tc.stats.TotalRequests, 1)

	entry, exists := tc.entries[shardID]
	if !exists {
		atomic.AddInt64(&tc.stats.CacheMisses, 1)
		return nil, false
	}

	// 检查过期
	if tc.config.CacheTTL > 0 && time.Since(entry.Timestamp) > tc.config.CacheTTL {
		tc.evictEntry(shardID)
		atomic.AddInt64(&tc.stats.CacheMisses, 1)
		return nil, false
	}

	// 更新访问计数和LRU位置
	atomic.AddInt64(&entry.AccessCount, 1)
	tc.moveToFront(shardID)

	atomic.AddInt64(&tc.stats.CacheHits, 1)
	tc.updateHitRatio()

	// 返回副本
	shardCopy := *entry.ShardInfo
	return &shardCopy, true
}

// Set 设置分片信息到缓存
func (tc *TopologyCache) Set(shardInfo *ShardInfo) {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	shardID := shardInfo.ID

	// 检查容量限制
	if len(tc.entries) >= tc.config.MaxCacheSize && tc.entries[shardID] == nil {
		tc.evictOldest()
	}

	// 创建缓存条目
	entry := &TopologyCacheEntry{
		ShardInfo:   shardInfo,
		Timestamp:   time.Now(),
		Version:     shardInfo.Version,
		AccessCount: 0,
	}

	tc.entries[shardID] = entry
	tc.addToLRU(shardID)

	// 更新统计信息
	tc.stats.CurrentSize = len(tc.entries)
	tc.stats.LastUpdate = time.Now()
}

// GetByKey 根据键获取对应的分片信息
func (tc *TopologyCache) GetByKey(key string) (*ShardInfo, bool) {
	tc.mu.RLock()
	shardID, exists := tc.keyToShard[key]
	tc.mu.RUnlock()

	if !exists {
		return nil, false
	}

	return tc.Get(shardID)
}

// SetKeyMapping 设置键到分片的映射
func (tc *TopologyCache) SetKeyMapping(key, shardID string) {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	tc.keyToShard[key] = shardID
}

// EvictShard 驱逐指定分片
func (tc *TopologyCache) EvictShard(shardID string) {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	tc.evictEntry(shardID)
}

// Clear 清空缓存
func (tc *TopologyCache) Clear() {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	tc.entries = make(map[string]*TopologyCacheEntry)
	tc.keyToShard = make(map[string]string)
	tc.entryNodes = make(map[string]*cacheNode)

	// 重置LRU链表
	tc.lruList.next = tc.lruTail
	tc.lruTail.prev = tc.lruList

	tc.stats.CurrentSize = 0
}

// GetStats 获取缓存统计信息
func (tc *TopologyCache) GetStats() *TopologyCacheStats {
	tc.mu.RLock()
	defer tc.mu.RUnlock()

	statsCopy := *tc.stats
	return &statsCopy
}

// UpdateVersion 更新全局版本号
func (tc *TopologyCache) UpdateVersion(version int64) {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	if version > tc.version {
		tc.version = version
		// 检查版本容差，清理过期缓存
		if tc.config.EnableVersionCheck {
			tc.cleanupOldVersions(version)
		}
	}
}

// 内部方法：驱逐最旧的缓存条目
func (tc *TopologyCache) evictOldest() {
	if tc.lruTail.prev == tc.lruList {
		return // 空链表
	}

	oldestNode := tc.lruTail.prev
	tc.evictEntry(oldestNode.shardID)
}

// 内部方法：驱逐指定条目
func (tc *TopologyCache) evictEntry(shardID string) {
	delete(tc.entries, shardID)
	tc.removeFromLRU(shardID)

	// 清理键映射
	for key, sid := range tc.keyToShard {
		if sid == shardID {
			delete(tc.keyToShard, key)
		}
	}

	atomic.AddInt64(&tc.stats.EvictionCount, 1)
	tc.stats.CurrentSize = len(tc.entries)
}

// 内部方法：添加到LRU链表头部
func (tc *TopologyCache) addToLRU(shardID string) {
	node := &cacheNode{shardID: shardID}
	node.next = tc.lruList.next
	node.prev = tc.lruList
	tc.lruList.next.prev = node
	tc.lruList.next = node

	tc.entryNodes[shardID] = node
}

// 内部方法：从LRU链表移除
func (tc *TopologyCache) removeFromLRU(shardID string) {
	node, exists := tc.entryNodes[shardID]
	if !exists {
		return
	}

	node.prev.next = node.next
	node.next.prev = node.prev
	delete(tc.entryNodes, shardID)
}

// 内部方法：将节点移到链表头部
func (tc *TopologyCache) moveToFront(shardID string) {
	tc.removeFromLRU(shardID)
	tc.addToLRU(shardID)
}

// 内部方法：清理旧版本缓存
func (tc *TopologyCache) cleanupOldVersions(currentVersion int64) {
	tolerance := int64(tc.config.VersionTolerance)
	minVersion := currentVersion - tolerance

	for shardID, entry := range tc.entries {
		if entry.Version < minVersion {
			tc.evictEntry(shardID)
		}
	}
}

// 内部方法：更新命中率
func (tc *TopologyCache) updateHitRatio() {
	total := atomic.LoadInt64(&tc.stats.TotalRequests)
	hits := atomic.LoadInt64(&tc.stats.CacheHits)

	if total > 0 {
		tc.stats.HitRatio = float64(hits) / float64(total)
	}
}

// TopologyEventSubscriber 拓扑事件订阅器
type TopologyEventSubscriber struct {
	mu            sync.RWMutex
	config        *TopologyConfig
	cache         *TopologyCache
	eventChannel  chan TopologyEvent
	stopChannel   chan struct{}
	reconnectChan chan struct{}
	isRunning     int64
	listeners     []TopologyEventListener
}

// TopologyEvent 拓扑变更事件
type TopologyEvent struct {
	Type      TopologyEventType `json:"type"`      // 事件类型
	ShardID   string            `json:"shardId"`   // 分片ID
	ShardInfo *ShardInfo        `json:"shardInfo"` // 分片信息
	Version   int64             `json:"version"`   // 版本号
	Timestamp time.Time         `json:"timestamp"` // 时间戳
	Source    string            `json:"source"`    // 事件源
}

// TopologyEventType 拓扑事件类型
type TopologyEventType int

const (
	EventShardAdded     TopologyEventType = iota // 分片添加
	EventShardRemoved                            // 分片删除
	EventShardUpdated                            // 分片更新
	EventShardMigration                          // 分片迁移
	EventNodeAdded                               // 节点添加
	EventNodeRemoved                             // 节点删除
	EventNodeUpdated                             // 节点更新
)

func (t TopologyEventType) String() string {
	switch t {
	case EventShardAdded:
		return "ShardAdded"
	case EventShardRemoved:
		return "ShardRemoved"
	case EventShardUpdated:
		return "ShardUpdated"
	case EventShardMigration:
		return "ShardMigration"
	case EventNodeAdded:
		return "NodeAdded"
	case EventNodeRemoved:
		return "NodeRemoved"
	case EventNodeUpdated:
		return "NodeUpdated"
	default:
		return "Unknown"
	}
}

// TopologyEventListener 拓扑事件监听器接口
type TopologyEventListener interface {
	OnTopologyEvent(event TopologyEvent)
}

// NewTopologyEventSubscriber 创建新的拓扑事件订阅器
func NewTopologyEventSubscriber(config *TopologyConfig, cache *TopologyCache) *TopologyEventSubscriber {
	return &TopologyEventSubscriber{
		config:        config,
		cache:         cache,
		eventChannel:  make(chan TopologyEvent, 1000),
		stopChannel:   make(chan struct{}),
		reconnectChan: make(chan struct{}, 1),
		listeners:     make([]TopologyEventListener, 0),
	}
}

// Start 启动事件订阅器
func (tes *TopologyEventSubscriber) Start(ctx context.Context) error {
	if !atomic.CompareAndSwapInt64(&tes.isRunning, 0, 1) {
		return fmt.Errorf("事件订阅器已经在运行")
	}

	go tes.eventLoop(ctx)

	if tes.config.EnableEventStream {
		go tes.eventStreamLoop(ctx)
	}

	return nil
}

// Stop 停止事件订阅器
func (tes *TopologyEventSubscriber) Stop() {
	if atomic.CompareAndSwapInt64(&tes.isRunning, 1, 0) {
		close(tes.stopChannel)
	}
}

// AddListener 添加事件监听器
func (tes *TopologyEventSubscriber) AddListener(listener TopologyEventListener) {
	tes.mu.Lock()
	defer tes.mu.Unlock()

	tes.listeners = append(tes.listeners, listener)
}

// RemoveListener 移除事件监听器
func (tes *TopologyEventSubscriber) RemoveListener(listener TopologyEventListener) {
	tes.mu.Lock()
	defer tes.mu.Unlock()

	for i, l := range tes.listeners {
		if l == listener {
			tes.listeners = append(tes.listeners[:i], tes.listeners[i+1:]...)
			break
		}
	}
}

// PublishEvent 发布拓扑事件
func (tes *TopologyEventSubscriber) PublishEvent(event TopologyEvent) {
	select {
	case tes.eventChannel <- event:
	default:
		// 事件队列满，丢弃最旧的事件
		select {
		case <-tes.eventChannel:
		default:
		}
		select {
		case tes.eventChannel <- event:
		default:
		}
	}
}

// 内部方法：事件处理循环
func (tes *TopologyEventSubscriber) eventLoop(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case <-tes.stopChannel:
			return
		case event := <-tes.eventChannel:
			tes.handleEvent(event)
		}
	}
}

// 内部方法：事件流循环
func (tes *TopologyEventSubscriber) eventStreamLoop(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case <-tes.stopChannel:
			return
		case <-tes.reconnectChan:
			// 重连逻辑
			time.Sleep(tes.config.ReconnectInterval)
		default:
			// TODO: 实现实际的事件流连接逻辑
			// 这里应该连接到raftserver的事件流接口
			time.Sleep(time.Second)
		}
	}
}

// 内部方法：处理拓扑事件
func (tes *TopologyEventSubscriber) handleEvent(event TopologyEvent) {
	// 更新缓存
	switch event.Type {
	case EventShardAdded, EventShardUpdated:
		if event.ShardInfo != nil {
			tes.cache.Set(event.ShardInfo)
		}
	case EventShardRemoved:
		tes.cache.EvictShard(event.ShardID)
	case EventShardMigration:
		// 分片迁移时更新分片信息
		if event.ShardInfo != nil {
			tes.cache.Set(event.ShardInfo)
		}
	}

	// 更新缓存版本
	tes.cache.UpdateVersion(event.Version)

	// 通知监听器
	tes.mu.RLock()
	listeners := make([]TopologyEventListener, len(tes.listeners))
	copy(listeners, tes.listeners)
	tes.mu.RUnlock()

	for _, listener := range listeners {
		go func(l TopologyEventListener) {
			defer func() {
				if r := recover(); r != nil {
					// 记录错误，但不影响其他监听器
				}
			}()
			l.OnTopologyEvent(event)
		}(listener)
	}
}

// TopologyAwareClient 拓扑感知客户端
type TopologyAwareClient struct {
	*Client         // 嵌入现有客户端
	config          *TopologyConfig
	cache           *TopologyCache
	eventSubscriber *TopologyEventSubscriber
	mu              sync.RWMutex
	isInitialized   bool
	stopChannel     chan struct{}
}

// NewTopologyAwareClient 创建新的拓扑感知客户端
func NewTopologyAwareClient(baseConfig Config, topologyConfig *TopologyConfig) (*TopologyAwareClient, error) {
	// 创建基础客户端
	baseClient, err := NewClient(baseConfig)
	if err != nil {
		return nil, fmt.Errorf("创建基础客户端失败: %w", err)
	}

	if topologyConfig == nil {
		topologyConfig = DefaultTopologyConfig()
	}

	// 创建拓扑缓存
	cache := NewTopologyCache(topologyConfig)

	// 创建事件订阅器
	eventSubscriber := NewTopologyEventSubscriber(topologyConfig, cache)

	client := &TopologyAwareClient{
		Client:          baseClient,
		config:          topologyConfig,
		cache:           cache,
		eventSubscriber: eventSubscriber,
		stopChannel:     make(chan struct{}),
	}

	return client, nil
}

// Initialize 初始化拓扑感知客户端
func (tac *TopologyAwareClient) Initialize(ctx context.Context) error {
	tac.mu.Lock()
	defer tac.mu.Unlock()

	if tac.isInitialized {
		return nil
	}

	// 启动事件订阅器
	if err := tac.eventSubscriber.Start(ctx); err != nil {
		return fmt.Errorf("启动事件订阅器失败: %w", err)
	}

	// 初始化拓扑信息
	if err := tac.refreshTopology(ctx); err != nil {
		return fmt.Errorf("初始化拓扑信息失败: %w", err)
	}

	// 启动定期刷新
	if tac.config.RefreshInterval > 0 {
		go tac.refreshLoop(ctx)
	}

	tac.isInitialized = true
	return nil
}

// Close 关闭拓扑感知客户端
func (tac *TopologyAwareClient) Close() error {
	tac.mu.Lock()
	defer tac.mu.Unlock()

	if !tac.isInitialized {
		return nil
	}

	// 停止事件订阅器
	tac.eventSubscriber.Stop()

	// 停止刷新循环
	close(tac.stopChannel)

	// 关闭基础客户端
	if err := tac.Client.Close(); err != nil {
		return err
	}

	tac.isInitialized = false
	return nil
}

// GetShardInfo 获取键对应的分片信息
func (tac *TopologyAwareClient) GetShardInfo(key string) (*ShardInfo, error) {
	// 首先尝试从缓存获取
	if shardInfo, ok := tac.cache.GetByKey(key); ok {
		return shardInfo, nil
	}

	// 缓存未命中，从服务端获取
	shardInfo, err := tac.fetchShardInfoFromServer(key)
	if err != nil {
		return nil, err
	}

	// 更新缓存
	tac.cache.Set(shardInfo)
	tac.cache.SetKeyMapping(key, shardInfo.ID)

	return shardInfo, nil
}

// GetAllShards 获取所有分片信息
func (tac *TopologyAwareClient) GetAllShards() (map[string]*ShardInfo, error) {
	// TODO: 实现从服务端获取所有分片信息的逻辑
	return nil, fmt.Errorf("暂未实现")
}

// RefreshTopology 刷新拓扑信息
func (tac *TopologyAwareClient) RefreshTopology(ctx context.Context) error {
	return tac.refreshTopology(ctx)
}

// GetTopologyStats 获取拓扑统计信息
func (tac *TopologyAwareClient) GetTopologyStats() *TopologyCacheStats {
	return tac.cache.GetStats()
}

// AddTopologyEventListener 添加拓扑事件监听器
func (tac *TopologyAwareClient) AddTopologyEventListener(listener TopologyEventListener) {
	tac.eventSubscriber.AddListener(listener)
}

// RemoveTopologyEventListener 移除拓扑事件监听器
func (tac *TopologyAwareClient) RemoveTopologyEventListener(listener TopologyEventListener) {
	tac.eventSubscriber.RemoveListener(listener)
}

// 内部方法：从服务端获取分片信息
func (tac *TopologyAwareClient) fetchShardInfoFromServer(key string) (*ShardInfo, error) {
	// TODO: 实现实际的服务端通信逻辑
	// 这里应该调用raftserver的API获取分片信息

	// 暂时返回模拟数据
	shardInfo := &ShardInfo{
		ID:        "shard-1",
		Range:     ShardRange{StartHash: 0, EndHash: 1000},
		Primary:   NodeID("node-1"),
		Replicas:  []NodeID{NodeID("node-2"), NodeID("node-3")},
		State:     ShardStateActive,
		Version:   1,
		CreatedAt: time.Now(),
		UpdatedAt: time.Now(),
		Metadata:  make(map[string]string),
	}

	return shardInfo, nil
}

// 内部方法：刷新拓扑信息
func (tac *TopologyAwareClient) refreshTopology(ctx context.Context) error {
	// TODO: 实现从服务端批量获取拓扑信息的逻辑
	return nil
}

// 内部方法：定期刷新循环
func (tac *TopologyAwareClient) refreshLoop(ctx context.Context) {
	ticker := time.NewTicker(tac.config.RefreshInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-tac.stopChannel:
			return
		case <-ticker.C:
			if err := tac.refreshTopology(ctx); err != nil {
				// 记录错误但继续循环
			}
		}
	}
}
