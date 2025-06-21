/*
* @Author: Lzww0608
* @Date: 2025-6-21 21:20:51
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-21 21:20:51
* @Description: ConcordKV consistent hash ring implementation for sharding
 */

package sharding

import (
	"crypto/sha256"
	"encoding/binary"
	"fmt"
	"sort"
	"sync"

	"raftserver/raft"
)

// VirtualNode 虚拟节点结构
type VirtualNode struct {
	Hash    uint64      // 哈希值
	NodeID  raft.NodeID // 实际节点ID
	VNodeID int         // 虚拟节点ID
	Weight  float64     // 节点权重
	Address string      // 节点地址
}

// HashRingConfig 哈希环配置
type HashRingConfig struct {
	VirtualNodesPerNode  int     // 每个物理节点的虚拟节点数量，默认200
	LoadBalanceThreshold float64 // 负载平衡阈值，默认0.2 (±20%)
	HashFunction         string  // 哈希函数类型，默认"sha256"
}

// DefaultHashRingConfig 默认哈希环配置
func DefaultHashRingConfig() *HashRingConfig {
	return &HashRingConfig{
		VirtualNodesPerNode:  200,
		LoadBalanceThreshold: 0.2,
		HashFunction:         "sha256",
	}
}

// ConsistentHashRing 一致性哈希环
type ConsistentHashRing struct {
	mu            sync.RWMutex
	config        *HashRingConfig
	virtualNodes  []VirtualNode                 // 虚拟节点列表，按哈希值排序
	physicalNodes map[raft.NodeID]*PhysicalNode // 物理节点映射
	totalWeight   float64                       // 总权重
	stats         *HashRingStats                // 统计信息
}

// PhysicalNode 物理节点信息
type PhysicalNode struct {
	ID           raft.NodeID   // 节点ID
	Address      string        // 节点地址
	Weight       float64       // 节点权重
	VirtualNodes []VirtualNode // 该物理节点的虚拟节点列表
	LoadMetrics  *LoadMetrics  // 负载指标
	Status       NodeStatus    // 节点状态
}

// LoadMetrics 负载指标
type LoadMetrics struct {
	QPS          float64 // 每秒查询数
	StorageUsage float64 // 存储使用量(MB)
	CPUUsage     float64 // CPU使用率
	MemoryUsage  float64 // 内存使用率
	LastUpdate   int64   // 最后更新时间戳
}

// NodeStatus 节点状态
type NodeStatus int

const (
	NodeStatusActive NodeStatus = iota
	NodeStatusInactive
	NodeStatusMigrating
	NodeStatusDraining
)

func (s NodeStatus) String() string {
	switch s {
	case NodeStatusActive:
		return "Active"
	case NodeStatusInactive:
		return "Inactive"
	case NodeStatusMigrating:
		return "Migrating"
	case NodeStatusDraining:
		return "Draining"
	default:
		return "Unknown"
	}
}

// HashRingStats 哈希环统计信息
type HashRingStats struct {
	TotalNodes        int     // 总物理节点数
	TotalVirtualNodes int     // 总虚拟节点数
	AverageLoad       float64 // 平均负载
	LoadVariance      float64 // 负载方差
	RebalanceCount    int64   // 重平衡次数
	MigrationCount    int64   // 数据迁移次数
}

// NewConsistentHashRing 创建新的一致性哈希环
func NewConsistentHashRing(config *HashRingConfig) *ConsistentHashRing {
	if config == nil {
		config = DefaultHashRingConfig()
	}

	return &ConsistentHashRing{
		config:        config,
		virtualNodes:  make([]VirtualNode, 0),
		physicalNodes: make(map[raft.NodeID]*PhysicalNode),
		stats:         &HashRingStats{},
	}
}

// AddNode 添加物理节点到哈希环
func (h *ConsistentHashRing) AddNode(nodeID raft.NodeID, address string, weight float64) error {
	h.mu.Lock()
	defer h.mu.Unlock()

	// 检查节点是否已存在
	if _, exists := h.physicalNodes[nodeID]; exists {
		return fmt.Errorf("节点 %s 已存在", nodeID)
	}

	// 创建物理节点
	physicalNode := &PhysicalNode{
		ID:           nodeID,
		Address:      address,
		Weight:       weight,
		VirtualNodes: make([]VirtualNode, 0, h.config.VirtualNodesPerNode),
		LoadMetrics:  &LoadMetrics{},
		Status:       NodeStatusActive,
	}

	// 生成虚拟节点
	for i := 0; i < h.config.VirtualNodesPerNode; i++ {
		vnode := h.createVirtualNode(nodeID, address, i, weight)
		physicalNode.VirtualNodes = append(physicalNode.VirtualNodes, vnode)
		h.virtualNodes = append(h.virtualNodes, vnode)
	}

	// 添加到物理节点映射
	h.physicalNodes[nodeID] = physicalNode
	h.totalWeight += weight

	// 重新排序虚拟节点
	h.sortVirtualNodes()

	// 更新统计信息
	h.updateStats()

	return nil
}

// RemoveNode 从哈希环中移除物理节点
func (h *ConsistentHashRing) RemoveNode(nodeID raft.NodeID) error {
	h.mu.Lock()
	defer h.mu.Unlock()

	// 检查节点是否存在
	physicalNode, exists := h.physicalNodes[nodeID]
	if !exists {
		return fmt.Errorf("节点 %s 不存在", nodeID)
	}

	// 从虚拟节点列表中移除该节点的所有虚拟节点
	newVirtualNodes := make([]VirtualNode, 0, len(h.virtualNodes)-len(physicalNode.VirtualNodes))
	for _, vnode := range h.virtualNodes {
		if vnode.NodeID != nodeID {
			newVirtualNodes = append(newVirtualNodes, vnode)
		}
	}
	h.virtualNodes = newVirtualNodes

	// 从物理节点映射中移除
	h.totalWeight -= physicalNode.Weight
	delete(h.physicalNodes, nodeID)

	// 更新统计信息
	h.updateStats()

	return nil
}

// GetNode 根据键获取对应的节点
func (h *ConsistentHashRing) GetNode(key string) (raft.NodeID, error) {
	h.mu.RLock()
	defer h.mu.RUnlock()

	if len(h.virtualNodes) == 0 {
		return "", fmt.Errorf("哈希环中没有可用节点")
	}

	// 计算键的哈希值
	keyHash := h.hash(key)

	// 使用二分查找找到第一个哈希值大于等于键哈希值的虚拟节点
	idx := sort.Search(len(h.virtualNodes), func(i int) bool {
		return h.virtualNodes[i].Hash >= keyHash
	})

	// 如果没找到，则使用第一个虚拟节点（环形特性）
	if idx == len(h.virtualNodes) {
		idx = 0
	}

	return h.virtualNodes[idx].NodeID, nil
}

// GetNodes 根据键获取多个节点（用于副本）
func (h *ConsistentHashRing) GetNodes(key string, count int) ([]raft.NodeID, error) {
	h.mu.RLock()
	defer h.mu.RUnlock()

	if len(h.physicalNodes) == 0 {
		return nil, fmt.Errorf("哈希环中没有可用节点")
	}

	if count <= 0 {
		count = 1
	}

	// 计算键的哈希值
	keyHash := h.hash(key)

	// 找到起始位置
	startIdx := sort.Search(len(h.virtualNodes), func(i int) bool {
		return h.virtualNodes[i].Hash >= keyHash
	})
	if startIdx == len(h.virtualNodes) {
		startIdx = 0
	}

	// 收集不同的物理节点
	nodeSet := make(map[raft.NodeID]bool)
	nodes := make([]raft.NodeID, 0, count)

	// 从起始位置开始，顺时针遍历虚拟节点
	for i := 0; i < len(h.virtualNodes) && len(nodes) < count; i++ {
		idx := (startIdx + i) % len(h.virtualNodes)
		nodeID := h.virtualNodes[idx].NodeID

		// 只有当该物理节点尚未被选中且状态为活跃时才添加
		if !nodeSet[nodeID] {
			if physicalNode, exists := h.physicalNodes[nodeID]; exists && physicalNode.Status == NodeStatusActive {
				nodeSet[nodeID] = true
				nodes = append(nodes, nodeID)
			}
		}
	}

	if len(nodes) == 0 {
		return nil, fmt.Errorf("没有可用的活跃节点")
	}

	return nodes, nil
}

// UpdateNodeWeight 更新节点权重
func (h *ConsistentHashRing) UpdateNodeWeight(nodeID raft.NodeID, newWeight float64) error {
	h.mu.Lock()
	defer h.mu.Unlock()

	physicalNode, exists := h.physicalNodes[nodeID]
	if !exists {
		return fmt.Errorf("节点 %s 不存在", nodeID)
	}

	// 更新总权重
	h.totalWeight = h.totalWeight - physicalNode.Weight + newWeight
	physicalNode.Weight = newWeight

	// 重新生成该节点的虚拟节点
	h.regenerateVirtualNodes(nodeID, newWeight)

	// 重新排序虚拟节点
	h.sortVirtualNodes()

	// 更新统计信息
	h.updateStats()

	return nil
}

// UpdateLoadMetrics 更新节点负载指标
func (h *ConsistentHashRing) UpdateLoadMetrics(nodeID raft.NodeID, metrics *LoadMetrics) error {
	h.mu.Lock()
	defer h.mu.Unlock()

	physicalNode, exists := h.physicalNodes[nodeID]
	if !exists {
		return fmt.Errorf("节点 %s 不存在", nodeID)
	}

	physicalNode.LoadMetrics = metrics
	return nil
}

// GetLoadImbalance 获取负载不平衡信息
func (h *ConsistentHashRing) GetLoadImbalance() map[raft.NodeID]float64 {
	h.mu.RLock()
	defer h.mu.RUnlock()

	imbalance := make(map[raft.NodeID]float64)

	if len(h.physicalNodes) == 0 {
		return imbalance
	}

	// 计算平均负载
	totalLoad := 0.0
	for _, node := range h.physicalNodes {
		totalLoad += node.LoadMetrics.QPS
	}
	avgLoad := totalLoad / float64(len(h.physicalNodes))

	// 计算每个节点的负载偏差
	for nodeID, node := range h.physicalNodes {
		if avgLoad > 0 {
			deviation := (node.LoadMetrics.QPS - avgLoad) / avgLoad
			if abs(deviation) > h.config.LoadBalanceThreshold {
				imbalance[nodeID] = deviation
			}
		}
	}

	return imbalance
}

// GetStats 获取哈希环统计信息
func (h *ConsistentHashRing) GetStats() *HashRingStats {
	h.mu.RLock()
	defer h.mu.RUnlock()

	// 创建统计信息副本
	stats := *h.stats
	return &stats
}

// GetPhysicalNodes 获取所有物理节点信息
func (h *ConsistentHashRing) GetPhysicalNodes() map[raft.NodeID]*PhysicalNode {
	h.mu.RLock()
	defer h.mu.RUnlock()

	// 创建深拷贝
	nodes := make(map[raft.NodeID]*PhysicalNode)
	for id, node := range h.physicalNodes {
		nodeCopy := *node
		nodes[id] = &nodeCopy
	}
	return nodes
}

// createVirtualNode 创建虚拟节点
func (h *ConsistentHashRing) createVirtualNode(nodeID raft.NodeID, address string, vnodeID int, weight float64) VirtualNode {
	// 生成虚拟节点的唯一标识符
	vnodeKey := fmt.Sprintf("%s:%d", nodeID, vnodeID)
	hash := h.hash(vnodeKey)

	return VirtualNode{
		Hash:    hash,
		NodeID:  nodeID,
		VNodeID: vnodeID,
		Weight:  weight,
		Address: address,
	}
}

// regenerateVirtualNodes 重新生成指定节点的虚拟节点
func (h *ConsistentHashRing) regenerateVirtualNodes(nodeID raft.NodeID, weight float64) {
	physicalNode := h.physicalNodes[nodeID]

	// 从虚拟节点列表中移除旧的虚拟节点
	newVirtualNodes := make([]VirtualNode, 0, len(h.virtualNodes))
	for _, vnode := range h.virtualNodes {
		if vnode.NodeID != nodeID {
			newVirtualNodes = append(newVirtualNodes, vnode)
		}
	}

	// 生成新的虚拟节点
	physicalNode.VirtualNodes = make([]VirtualNode, 0, h.config.VirtualNodesPerNode)
	for i := 0; i < h.config.VirtualNodesPerNode; i++ {
		vnode := h.createVirtualNode(nodeID, physicalNode.Address, i, weight)
		physicalNode.VirtualNodes = append(physicalNode.VirtualNodes, vnode)
		newVirtualNodes = append(newVirtualNodes, vnode)
	}

	h.virtualNodes = newVirtualNodes
}

// sortVirtualNodes 对虚拟节点按哈希值排序
func (h *ConsistentHashRing) sortVirtualNodes() {
	sort.Slice(h.virtualNodes, func(i, j int) bool {
		return h.virtualNodes[i].Hash < h.virtualNodes[j].Hash
	})
}

// hash 计算字符串的哈希值
func (h *ConsistentHashRing) hash(key string) uint64 {
	switch h.config.HashFunction {
	case "sha256":
		return h.sha256Hash(key)
	default:
		return h.sha256Hash(key)
	}
}

// sha256Hash 使用SHA256计算哈希值
func (h *ConsistentHashRing) sha256Hash(key string) uint64 {
	hasher := sha256.New()
	hasher.Write([]byte(key))
	hash := hasher.Sum(nil)
	return binary.BigEndian.Uint64(hash[:8])
}

// updateStats 更新统计信息
func (h *ConsistentHashRing) updateStats() {
	h.stats.TotalNodes = len(h.physicalNodes)
	h.stats.TotalVirtualNodes = len(h.virtualNodes)

	if len(h.physicalNodes) > 0 {
		// 计算平均负载
		totalLoad := 0.0
		for _, node := range h.physicalNodes {
			totalLoad += node.LoadMetrics.QPS
		}
		h.stats.AverageLoad = totalLoad / float64(len(h.physicalNodes))

		// 计算负载方差
		variance := 0.0
		for _, node := range h.physicalNodes {
			diff := node.LoadMetrics.QPS - h.stats.AverageLoad
			variance += diff * diff
		}
		h.stats.LoadVariance = variance / float64(len(h.physicalNodes))
	}
}

// abs 计算绝对值
func abs(x float64) float64 {
	if x < 0 {
		return -x
	}
	return x
}
