package raft

import (
	"encoding/json"
	"fmt"
	"time"
)

// MembershipChangeType 成员变更类型
type MembershipChangeType int

const (
	// AddServer 添加服务器
	AddServer MembershipChangeType = iota
	// RemoveServer 移除服务器
	RemoveServer
)

// MembershipChange 成员变更请求
type MembershipChange struct {
	Type   MembershipChangeType `json:"type"`
	Server Server               `json:"server"`
}

// ConfigurationState 配置状态
type ConfigurationState int

const (
	// ConfigurationStable 稳定配置
	ConfigurationStable ConfigurationState = iota
	// ConfigurationJoint 联合配置
	ConfigurationJoint
)

// JointConfiguration 联合配置
type JointConfiguration struct {
	OldConfiguration Configuration      `json:"oldConfiguration"`
	NewConfiguration Configuration      `json:"newConfiguration"`
	State            ConfigurationState `json:"state"`
}

// AddServer 添加服务器到集群
func (n *Node) AddServer(server Server) error {
	n.mu.Lock()
	defer n.mu.Unlock()

	if n.state != Leader {
		return ErrNotLeader
	}

	// 检查服务器是否已存在
	for _, s := range n.config.Servers {
		if s.ID == server.ID {
			return fmt.Errorf("服务器 %s 已存在", server.ID)
		}
	}

	n.logger.Printf("开始添加服务器: %s (%s)", server.ID, server.Address)

	// 创建成员变更命令
	change := MembershipChange{
		Type:   AddServer,
		Server: server,
	}

	data, err := json.Marshal(change)
	if err != nil {
		return fmt.Errorf("序列化成员变更失败: %w", err)
	}

	// 创建配置变更日志条目
	entry := &LogEntry{
		Index:     n.storage.GetLastLogIndex() + 1,
		Term:      n.getCurrentTerm(),
		Timestamp: time.Now(),
		Type:      EntryConfiguration,
		Data:      data,
	}

	// 保存到本地日志
	if err := n.storage.SaveLogEntries([]LogEntry{*entry}); err != nil {
		return fmt.Errorf("保存配置变更日志失败: %w", err)
	}

	n.logger.Printf("已提议添加服务器 %s 的配置变更，日志索引: %d", server.ID, entry.Index)

	// 复制到跟随者
	go n.sendHeartbeats()

	return nil
}

// RemoveServer 从集群中移除服务器
func (n *Node) RemoveServer(serverID NodeID) error {
	n.mu.Lock()
	defer n.mu.Unlock()

	if n.state != Leader {
		return ErrNotLeader
	}

	// 查找要移除的服务器
	var serverToRemove *Server
	for _, s := range n.config.Servers {
		if s.ID == serverID {
			serverToRemove = &s
			break
		}
	}

	if serverToRemove == nil {
		return fmt.Errorf("服务器 %s 不存在", serverID)
	}

	// 不能移除自己（领导者）
	if serverID == n.id {
		return fmt.Errorf("不能移除当前领导者")
	}

	n.logger.Printf("开始移除服务器: %s (%s)", serverID, serverToRemove.Address)

	// 创建成员变更命令
	change := MembershipChange{
		Type:   RemoveServer,
		Server: *serverToRemove,
	}

	data, err := json.Marshal(change)
	if err != nil {
		return fmt.Errorf("序列化成员变更失败: %w", err)
	}

	// 创建配置变更日志条目
	entry := &LogEntry{
		Index:     n.storage.GetLastLogIndex() + 1,
		Term:      n.getCurrentTerm(),
		Timestamp: time.Now(),
		Type:      EntryConfiguration,
		Data:      data,
	}

	// 保存到本地日志
	if err := n.storage.SaveLogEntries([]LogEntry{*entry}); err != nil {
		return fmt.Errorf("保存配置变更日志失败: %w", err)
	}

	n.logger.Printf("已提议移除服务器 %s 的配置变更，日志索引: %d", serverID, entry.Index)

	// 复制到跟随者
	go n.sendHeartbeats()

	return nil
}

// applyConfigurationChange 应用配置变更
func (n *Node) applyConfigurationChange(entry *LogEntry) error {
	var change MembershipChange
	if err := json.Unmarshal(entry.Data, &change); err != nil {
		return fmt.Errorf("反序列化成员变更失败: %w", err)
	}

	n.mu.Lock()
	defer n.mu.Unlock()

	switch change.Type {
	case AddServer:
		return n.applyAddServer(change.Server)
	case RemoveServer:
		return n.applyRemoveServer(change.Server.ID)
	default:
		return fmt.Errorf("未知的成员变更类型: %d", change.Type)
	}
}

// applyAddServer 应用添加服务器
func (n *Node) applyAddServer(server Server) error {
	// 检查服务器是否已存在
	for _, s := range n.config.Servers {
		if s.ID == server.ID {
			n.logger.Printf("服务器 %s 已存在，跳过添加", server.ID)
			return nil
		}
	}

	// 添加到配置
	n.config.Servers = append(n.config.Servers, server)

	// 如果是领导者，初始化新服务器的状态
	if n.state == Leader {
		n.nextIndex[server.ID] = n.storage.GetLastLogIndex() + 1
		n.matchIndex[server.ID] = 0
	}

	n.logger.Printf("成功添加服务器: %s (%s)", server.ID, server.Address)
	return nil
}

// applyRemoveServer 应用移除服务器
func (n *Node) applyRemoveServer(serverID NodeID) error {
	// 查找并移除服务器
	newServers := make([]Server, 0, len(n.config.Servers))
	found := false

	for _, s := range n.config.Servers {
		if s.ID != serverID {
			newServers = append(newServers, s)
		} else {
			found = true
		}
	}

	if !found {
		n.logger.Printf("服务器 %s 不存在，跳过移除", serverID)
		return nil
	}

	// 更新配置
	n.config.Servers = newServers

	// 如果是领导者，清理服务器状态
	if n.state == Leader {
		delete(n.nextIndex, serverID)
		delete(n.matchIndex, serverID)
	}

	// 如果移除的是自己，转为跟随者并停止
	if serverID == n.id {
		n.logger.Printf("自己被移除，转为跟随者")
		n.becomeFollower(n.getCurrentTerm(), "")
		// 在实际实现中，这里可能需要优雅关闭
	}

	n.logger.Printf("成功移除服务器: %s", serverID)
	return nil
}

// GetConfiguration 获取当前配置
func (n *Node) GetConfiguration() Configuration {
	n.mu.RLock()
	defer n.mu.RUnlock()

	servers := make([]Server, len(n.config.Servers))
	copy(servers, n.config.Servers)

	return Configuration{
		Servers: servers,
	}
}

// IsConfigurationChanging 检查是否正在进行配置变更
func (n *Node) IsConfigurationChanging() bool {
	// 简化实现：检查最后几个日志条目是否有配置变更
	lastIndex := n.storage.GetLastLogIndex()

	// 检查最近的几个条目
	for i := lastIndex; i > 0 && i > lastIndex-5; i-- {
		entry, err := n.storage.GetLogEntry(i)
		if err != nil {
			continue
		}

		if entry.Type == EntryConfiguration && i > n.commitIndex {
			return true // 有未提交的配置变更
		}
	}

	return false
}

// validateConfiguration 验证配置的有效性
func (n *Node) validateConfiguration(config Configuration) error {
	if len(config.Servers) == 0 {
		return fmt.Errorf("配置不能为空")
	}

	// 检查重复的服务器ID
	seen := make(map[NodeID]bool)
	for _, server := range config.Servers {
		if seen[server.ID] {
			return fmt.Errorf("重复的服务器ID: %s", server.ID)
		}
		seen[server.ID] = true
	}

	return nil
}
