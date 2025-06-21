/*
* @Author: Lzww0608
* @Date: 2025-6-21 21:21:06
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-21 21:21:06
* @Description: ConcordKV shard metadata management for distributed sharding
 */

package sharding

import (
	"fmt"
	"sync"
	"time"

	"raftserver/raft"
)

// ShardState 分片状态
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

// ShardRange 分片范围定义
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

// ShardInfo 分片信息
type ShardInfo struct {
	ID        string            `json:"id"`        // 分片ID
	Range     ShardRange        `json:"range"`     // 分片范围
	Primary   raft.NodeID       `json:"primary"`   // 主节点
	Replicas  []raft.NodeID     `json:"replicas"`  // 副本节点列表
	State     ShardState        `json:"state"`     // 分片状态
	Version   int64             `json:"version"`   // 版本号
	CreatedAt time.Time         `json:"createdAt"` // 创建时间
	UpdatedAt time.Time         `json:"updatedAt"` // 更新时间
	Metadata  map[string]string `json:"metadata"`  // 元数据
	Migration *MigrationInfo    `json:"migration"` // 迁移信息（如果正在迁移）
}

// MigrationInfo 迁移信息
type MigrationInfo struct {
	SourceNode  raft.NodeID `json:"sourceNode"`  // 源节点
	TargetNode  raft.NodeID `json:"targetNode"`  // 目标节点
	Progress    float64     `json:"progress"`    // 迁移进度 (0.0-1.0)
	StartedAt   time.Time   `json:"startedAt"`   // 开始时间
	EstimatedET time.Time   `json:"estimatedET"` // 预计完成时间
	Status      string      `json:"status"`      // 迁移状态
}

// ShardMetadataConfig 分片元数据配置
type ShardMetadataConfig struct {
	DefaultReplicationFactor int           // 默认副本因子
	ShardCount               int           // 分片数量
	PersistenceInterval      time.Duration // 持久化间隔
	StoragePath              string        // 存储路径
}

// DefaultShardMetadataConfig 默认分片元数据配置
func DefaultShardMetadataConfig() *ShardMetadataConfig {
	return &ShardMetadataConfig{
		DefaultReplicationFactor: 3,
		ShardCount:               256,
		PersistenceInterval:      30 * time.Second,
		StoragePath:              "./shard_metadata",
	}
}

// ShardMetadataManager 分片元数据管理器
type ShardMetadataManager struct {
	mu         sync.RWMutex
	config     *ShardMetadataConfig
	shards     map[string]*ShardInfo    // 分片ID到分片信息的映射
	nodeShards map[raft.NodeID][]string // 节点ID到分片ID列表的映射
	hashRing   *ConsistentHashRing      // 一致性哈希环引用
	version    int64                    // 全局版本号
	storage    ShardMetadataStorage     // 持久化存储接口
	stats      *ShardMetadataStats      // 统计信息
}

// ShardMetadataStorage 分片元数据存储接口
type ShardMetadataStorage interface {
	// SaveShardInfo 保存分片信息
	SaveShardInfo(shard *ShardInfo) error

	// LoadShardInfo 加载分片信息
	LoadShardInfo(shardID string) (*ShardInfo, error)

	// LoadAllShards 加载所有分片信息
	LoadAllShards() (map[string]*ShardInfo, error)

	// DeleteShardInfo 删除分片信息
	DeleteShardInfo(shardID string) error

	// SaveMetadata 保存元数据
	SaveMetadata(key string, value []byte) error

	// LoadMetadata 加载元数据
	LoadMetadata(key string) ([]byte, error)
}

// ShardMetadataStats 分片元数据统计信息
type ShardMetadataStats struct {
	TotalShards       int                 // 总分片数
	ActiveShards      int                 // 活跃分片数
	MigratingShards   int                 // 迁移中分片数
	NodesDistribution map[raft.NodeID]int // 节点分片分布
	StateDistribution map[ShardState]int  // 状态分布
	LastUpdate        time.Time           // 最后更新时间
}

// NewShardMetadataManager 创建新的分片元数据管理器
func NewShardMetadataManager(config *ShardMetadataConfig, hashRing *ConsistentHashRing, storage ShardMetadataStorage) *ShardMetadataManager {
	if config == nil {
		config = DefaultShardMetadataConfig()
	}

	manager := &ShardMetadataManager{
		config:     config,
		shards:     make(map[string]*ShardInfo),
		nodeShards: make(map[raft.NodeID][]string),
		hashRing:   hashRing,
		version:    1,
		storage:    storage,
		stats: &ShardMetadataStats{
			NodesDistribution: make(map[raft.NodeID]int),
			StateDistribution: make(map[ShardState]int),
		},
	}

	// 尝试从存储中恢复数据
	if err := manager.loadFromStorage(); err != nil {
		// 如果加载失败，初始化默认分片
		manager.initializeDefaultShards()
	}

	return manager
}

// CreateShard 创建新分片
func (sm *ShardMetadataManager) CreateShard(shardID string, shardRange ShardRange, primary raft.NodeID, replicas []raft.NodeID) error {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	// 检查分片是否已存在
	if _, exists := sm.shards[shardID]; exists {
		return fmt.Errorf("分片 %s 已存在", shardID)
	}

	// 创建分片信息
	shard := &ShardInfo{
		ID:        shardID,
		Range:     shardRange,
		Primary:   primary,
		Replicas:  replicas,
		State:     ShardStateActive,
		Version:   sm.nextVersion(),
		CreatedAt: time.Now(),
		UpdatedAt: time.Now(),
		Metadata:  make(map[string]string),
	}

	// 添加到内存
	sm.shards[shardID] = shard
	sm.addShardToNode(primary, shardID)
	for _, replica := range replicas {
		sm.addShardToNode(replica, shardID)
	}

	// 持久化
	if err := sm.storage.SaveShardInfo(shard); err != nil {
		// 回滚内存操作
		delete(sm.shards, shardID)
		sm.removeShardFromNode(primary, shardID)
		for _, replica := range replicas {
			sm.removeShardFromNode(replica, shardID)
		}
		return fmt.Errorf("保存分片信息失败: %w", err)
	}

	// 更新统计信息
	sm.updateStats()

	return nil
}

// GetShard 获取分片信息
func (sm *ShardMetadataManager) GetShard(shardID string) (*ShardInfo, error) {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	shard, exists := sm.shards[shardID]
	if !exists {
		return nil, fmt.Errorf("分片 %s 不存在", shardID)
	}

	// 返回副本
	shardCopy := *shard
	return &shardCopy, nil
}

// GetShardByKey 根据键获取对应的分片信息
func (sm *ShardMetadataManager) GetShardByKey(key string) (*ShardInfo, error) {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	// 使用哈希环计算键的哈希值
	keyHash := sm.hashRing.hash(key)

	// 查找包含该哈希值的分片
	for _, shard := range sm.shards {
		if shard.Range.Contains(keyHash) {
			shardCopy := *shard
			return &shardCopy, nil
		}
	}

	return nil, fmt.Errorf("未找到键 %s 对应的分片", key)
}

// UpdateShardState 更新分片状态
func (sm *ShardMetadataManager) UpdateShardState(shardID string, newState ShardState) error {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	shard, exists := sm.shards[shardID]
	if !exists {
		return fmt.Errorf("分片 %s 不存在", shardID)
	}

	oldState := shard.State
	shard.State = newState
	shard.Version = sm.nextVersion()
	shard.UpdatedAt = time.Now()

	// 持久化
	if err := sm.storage.SaveShardInfo(shard); err != nil {
		// 回滚
		shard.State = oldState
		return fmt.Errorf("保存分片状态失败: %w", err)
	}

	// 更新统计信息
	sm.updateStats()

	return nil
}

// StartMigration 开始分片迁移
func (sm *ShardMetadataManager) StartMigration(shardID string, sourceNode, targetNode raft.NodeID) error {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	shard, exists := sm.shards[shardID]
	if !exists {
		return fmt.Errorf("分片 %s 不存在", shardID)
	}

	if shard.State != ShardStateActive {
		return fmt.Errorf("分片 %s 状态不是活跃状态，无法开始迁移", shardID)
	}

	// 设置迁移信息
	shard.Migration = &MigrationInfo{
		SourceNode:  sourceNode,
		TargetNode:  targetNode,
		Progress:    0.0,
		StartedAt:   time.Now(),
		EstimatedET: time.Now().Add(time.Hour), // 预计1小时完成
		Status:      "starting",
	}

	shard.State = ShardStateMigrating
	shard.Version = sm.nextVersion()
	shard.UpdatedAt = time.Now()

	// 持久化
	if err := sm.storage.SaveShardInfo(shard); err != nil {
		// 回滚
		shard.Migration = nil
		shard.State = ShardStateActive
		return fmt.Errorf("保存迁移信息失败: %w", err)
	}

	// 更新统计信息
	sm.updateStats()

	return nil
}

// UpdateMigrationProgress 更新迁移进度
func (sm *ShardMetadataManager) UpdateMigrationProgress(shardID string, progress float64) error {
	sm.mu.Lock()
	defer sm.mu.Unlock()

	shard, exists := sm.shards[shardID]
	if !exists {
		return fmt.Errorf("分片 %s 不存在", shardID)
	}

	if shard.Migration == nil {
		return fmt.Errorf("分片 %s 没有正在进行的迁移", shardID)
	}

	shard.Migration.Progress = progress
	shard.Version = sm.nextVersion()
	shard.UpdatedAt = time.Now()

	// 如果迁移完成，更新分片信息
	if progress >= 1.0 {
		shard.Migration.Status = "completed"
		shard.State = ShardStateActive

		// 更新主节点（如果这是主节点迁移）
		if shard.Primary == shard.Migration.SourceNode {
			shard.Primary = shard.Migration.TargetNode
		}

		// 更新副本列表
		for i, replica := range shard.Replicas {
			if replica == shard.Migration.SourceNode {
				shard.Replicas[i] = shard.Migration.TargetNode
				break
			}
		}

		// 更新节点分片映射
		sm.removeShardFromNode(shard.Migration.SourceNode, shardID)
		sm.addShardToNode(shard.Migration.TargetNode, shardID)

		// 清除迁移信息
		shard.Migration = nil
	}

	// 持久化
	if err := sm.storage.SaveShardInfo(shard); err != nil {
		return fmt.Errorf("保存迁移进度失败: %w", err)
	}

	// 更新统计信息
	sm.updateStats()

	return nil
}

// GetNodeShards 获取节点的所有分片
func (sm *ShardMetadataManager) GetNodeShards(nodeID raft.NodeID) []string {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	shards, exists := sm.nodeShards[nodeID]
	if !exists {
		return []string{}
	}

	// 返回副本
	result := make([]string, len(shards))
	copy(result, shards)
	return result
}

// GetAllShards 获取所有分片信息
func (sm *ShardMetadataManager) GetAllShards() map[string]*ShardInfo {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	// 创建深拷贝
	result := make(map[string]*ShardInfo)
	for id, shard := range sm.shards {
		shardCopy := *shard
		result[id] = &shardCopy
	}
	return result
}

// GetStats 获取统计信息
func (sm *ShardMetadataManager) GetStats() *ShardMetadataStats {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	// 返回统计信息副本
	stats := *sm.stats
	stats.NodesDistribution = make(map[raft.NodeID]int)
	for k, v := range sm.stats.NodesDistribution {
		stats.NodesDistribution[k] = v
	}
	stats.StateDistribution = make(map[ShardState]int)
	for k, v := range sm.stats.StateDistribution {
		stats.StateDistribution[k] = v
	}
	return &stats
}

// 内部方法

// nextVersion 生成下一个版本号
func (sm *ShardMetadataManager) nextVersion() int64 {
	sm.version++
	return sm.version
}

// addShardToNode 将分片添加到节点映射
func (sm *ShardMetadataManager) addShardToNode(nodeID raft.NodeID, shardID string) {
	if _, exists := sm.nodeShards[nodeID]; !exists {
		sm.nodeShards[nodeID] = make([]string, 0)
	}
	sm.nodeShards[nodeID] = append(sm.nodeShards[nodeID], shardID)
}

// removeShardFromNode 从节点映射中移除分片
func (sm *ShardMetadataManager) removeShardFromNode(nodeID raft.NodeID, shardID string) {
	shards, exists := sm.nodeShards[nodeID]
	if !exists {
		return
	}

	for i, id := range shards {
		if id == shardID {
			sm.nodeShards[nodeID] = append(shards[:i], shards[i+1:]...)
			break
		}
	}

	// 如果节点没有分片了，删除映射
	if len(sm.nodeShards[nodeID]) == 0 {
		delete(sm.nodeShards, nodeID)
	}
}

// updateStats 更新统计信息
func (sm *ShardMetadataManager) updateStats() {
	sm.stats.TotalShards = len(sm.shards)
	sm.stats.ActiveShards = 0
	sm.stats.MigratingShards = 0

	// 重置分布统计
	sm.stats.NodesDistribution = make(map[raft.NodeID]int)
	sm.stats.StateDistribution = make(map[ShardState]int)

	for _, shard := range sm.shards {
		// 状态分布
		sm.stats.StateDistribution[shard.State]++

		if shard.State == ShardStateActive {
			sm.stats.ActiveShards++
		} else if shard.State == ShardStateMigrating {
			sm.stats.MigratingShards++
		}

		// 节点分布
		sm.stats.NodesDistribution[shard.Primary]++
		for _, replica := range shard.Replicas {
			sm.stats.NodesDistribution[replica]++
		}
	}

	sm.stats.LastUpdate = time.Now()
}

// loadFromStorage 从存储中加载数据
func (sm *ShardMetadataManager) loadFromStorage() error {
	shards, err := sm.storage.LoadAllShards()
	if err != nil {
		return err
	}

	sm.shards = shards

	// 重建节点分片映射
	sm.nodeShards = make(map[raft.NodeID][]string)
	maxVersion := int64(0)

	for shardID, shard := range shards {
		sm.addShardToNode(shard.Primary, shardID)
		for _, replica := range shard.Replicas {
			sm.addShardToNode(replica, shardID)
		}

		if shard.Version > maxVersion {
			maxVersion = shard.Version
		}
	}

	sm.version = maxVersion
	sm.updateStats()

	return nil
}

// initializeDefaultShards 初始化默认分片
func (sm *ShardMetadataManager) initializeDefaultShards() {
	// 如果分片数量为0，不创建任何分片
	if sm.config.ShardCount == 0 {
		return
	}

	// 获取哈希环中的节点
	physicalNodes := sm.hashRing.GetPhysicalNodes()
	if len(physicalNodes) == 0 {
		return
	}

	// 将节点转换为切片
	nodes := make([]raft.NodeID, 0, len(physicalNodes))
	for nodeID := range physicalNodes {
		nodes = append(nodes, nodeID)
	}

	// 计算每个分片的范围
	shardSize := ^uint64(0) / uint64(sm.config.ShardCount)

	for i := 0; i < sm.config.ShardCount; i++ {
		shardID := fmt.Sprintf("shard-%03d", i)
		startHash := uint64(i) * shardSize
		endHash := startHash + shardSize
		if i == sm.config.ShardCount-1 {
			endHash = ^uint64(0) // 最后一个分片包含到最大值
		}

		shardRange := ShardRange{
			StartHash: startHash,
			EndHash:   endHash,
		}

		// 选择主节点和副本节点
		primaryIdx := i % len(nodes)
		primary := nodes[primaryIdx]

		replicas := make([]raft.NodeID, 0, sm.config.DefaultReplicationFactor-1)
		for j := 1; j < sm.config.DefaultReplicationFactor && j < len(nodes); j++ {
			replicaIdx := (primaryIdx + j) % len(nodes)
			replicas = append(replicas, nodes[replicaIdx])
		}

		// 直接创建分片信息（不需要锁，因为在构造函数中）
		shard := &ShardInfo{
			ID:        shardID,
			Range:     shardRange,
			Primary:   primary,
			Replicas:  replicas,
			State:     ShardStateActive,
			Version:   sm.nextVersion(),
			CreatedAt: time.Now(),
			UpdatedAt: time.Now(),
			Metadata:  make(map[string]string),
		}

		// 添加到内存
		sm.shards[shardID] = shard
		sm.addShardToNode(primary, shardID)
		for _, replica := range replicas {
			sm.addShardToNode(replica, shardID)
		}

		// 尝试持久化（忽略错误，因为这是初始化）
		sm.storage.SaveShardInfo(shard)
	}

	// 更新统计信息
	sm.updateStats()
}
