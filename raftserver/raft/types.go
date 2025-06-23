/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-23 17:00:11
* @Description: ConcordKV Raft consensus server - types.go
 */
package raft

import (
	"context"
	"time"
)

// NodeState 代表Raft节点的状态
type NodeState int

const (
	// Follower 跟随者状态
	Follower NodeState = iota
	// Candidate 候选者状态
	Candidate
	// Leader 领导者状态
	Leader
)

func (s NodeState) String() string {
	switch s {
	case Follower:
		return "Follower"
	case Candidate:
		return "Candidate"
	case Leader:
		return "Leader"
	default:
		return "Unknown"
	}
}

// NodeID 节点标识符
type NodeID string

// Term 任期号
type Term uint64

// LogIndex 日志索引
type LogIndex uint64

// LogEntry 日志条目
type LogEntry struct {
	Index     LogIndex  `json:"index"`     // 日志索引
	Term      Term      `json:"term"`      // 任期号
	Timestamp time.Time `json:"timestamp"` // 时间戳
	Type      EntryType `json:"type"`      // 条目类型
	Data      []byte    `json:"data"`      // 数据
}

// EntryType 日志条目类型
type EntryType int

const (
	// EntryNormal 普通日志条目
	EntryNormal EntryType = iota
	// EntryConfiguration 配置变更条目
	EntryConfiguration
	// EntrySnapshot 快照条目
	EntrySnapshot
)

// VoteRequest 投票请求
type VoteRequest struct {
	Term         Term     `json:"term"`         // 候选人任期号
	CandidateID  NodeID   `json:"candidateId"`  // 候选人ID
	LastLogIndex LogIndex `json:"lastLogIndex"` // 候选人最后日志索引
	LastLogTerm  Term     `json:"lastLogTerm"`  // 候选人最后日志任期号
}

// VoteResponse 投票响应
type VoteResponse struct {
	Term        Term `json:"term"`        // 当前任期号
	VoteGranted bool `json:"voteGranted"` // 是否投票给候选人
}

// AppendEntriesRequest 追加日志请求
type AppendEntriesRequest struct {
	Term         Term       `json:"term"`         // 领导者任期号
	LeaderID     NodeID     `json:"leaderId"`     // 领导者ID
	PrevLogIndex LogIndex   `json:"prevLogIndex"` // 新日志前一个日志索引
	PrevLogTerm  Term       `json:"prevLogTerm"`  // 新日志前一个日志任期号
	Entries      []LogEntry `json:"entries"`      // 要追加的日志条目
	LeaderCommit LogIndex   `json:"leaderCommit"` // 领导者提交索引
}

// AppendEntriesResponse 追加日志响应
type AppendEntriesResponse struct {
	Term          Term     `json:"term"`          // 当前任期号
	Success       bool     `json:"success"`       // 是否成功
	ConflictIndex LogIndex `json:"conflictIndex"` // 冲突索引（用于快速回退）
	ConflictTerm  Term     `json:"conflictTerm"`  // 冲突任期
}

// InstallSnapshotRequest 安装快照请求
type InstallSnapshotRequest struct {
	Term              Term     `json:"term"`              // 领导者任期号
	LeaderID          NodeID   `json:"leaderId"`          // 领导者ID
	LastIncludedIndex LogIndex `json:"lastIncludedIndex"` // 快照最后包含的索引
	LastIncludedTerm  Term     `json:"lastIncludedTerm"`  // 快照最后包含的任期
	Offset            int64    `json:"offset"`            // 块在快照中的偏移量
	Data              []byte   `json:"data"`              // 快照数据块
	Done              bool     `json:"done"`              // 是否为最后一块
}

// InstallSnapshotResponse 安装快照响应
type InstallSnapshotResponse struct {
	Term Term `json:"term"` // 当前任期号
}

// Configuration 集群配置
type Configuration struct {
	Servers []Server `json:"servers"` // 服务器列表
}

// Server 服务器信息
type Server struct {
	ID      NodeID `json:"id"`      // 服务器ID
	Address string `json:"address"` // 服务器地址
}

// Snapshot 快照结构
type Snapshot struct {
	LastIncludedIndex LogIndex      `json:"lastIncludedIndex"` // 快照最后包含的索引
	LastIncludedTerm  Term          `json:"lastIncludedTerm"`  // 快照最后包含的任期
	Configuration     Configuration `json:"configuration"`     // 集群配置
	Data              []byte        `json:"data"`              // 快照数据
}

// Transport 网络传输接口
type Transport interface {
	// SendVoteRequest 发送投票请求
	SendVoteRequest(ctx context.Context, target NodeID, req *VoteRequest) (*VoteResponse, error)

	// SendAppendEntries 发送追加日志请求
	SendAppendEntries(ctx context.Context, target NodeID, req *AppendEntriesRequest) (*AppendEntriesResponse, error)

	// SendInstallSnapshot 发送安装快照请求
	SendInstallSnapshot(ctx context.Context, target NodeID, req *InstallSnapshotRequest) (*InstallSnapshotResponse, error)

	// Start 启动传输层
	Start() error

	// Stop 停止传输层
	Stop() error

	// LocalAddr 获取本地地址
	LocalAddr() string
}

// Storage 存储接口
type Storage interface {
	// SaveCurrentTerm 保存当前任期号
	SaveCurrentTerm(term Term) error

	// GetCurrentTerm 获取当前任期号
	GetCurrentTerm() (Term, error)

	// SaveVotedFor 保存投票给的候选人
	SaveVotedFor(candidateID NodeID) error

	// GetVotedFor 获取投票给的候选人
	GetVotedFor() (NodeID, error)

	// SaveLogEntries 保存日志条目
	SaveLogEntries(entries []LogEntry) error

	// GetLogEntry 获取指定索引的日志条目
	GetLogEntry(index LogIndex) (*LogEntry, error)

	// GetLogEntries 获取指定范围的日志条目
	GetLogEntries(start, end LogIndex) ([]LogEntry, error)

	// GetLastLogIndex 获取最后一个日志索引
	GetLastLogIndex() LogIndex

	// GetLastLogTerm 获取最后一个日志的任期号
	GetLastLogTerm() Term

	// TruncateLog 截断日志（删除指定索引之后的所有条目）
	TruncateLog(index LogIndex) error

	// SaveSnapshot 保存快照
	SaveSnapshot(snapshot *Snapshot) error

	// GetSnapshot 获取快照
	GetSnapshot() (*Snapshot, error)

	// Close 关闭存储
	Close() error
}

// StateMachine 状态机接口
type StateMachine interface {
	// Apply 应用日志条目到状态机
	Apply(entry *LogEntry) error

	// CreateSnapshot 创建状态机快照
	CreateSnapshot() ([]byte, error)

	// RestoreSnapshot 从快照恢复状态机
	RestoreSnapshot(data []byte) error
}

// Config Raft配置
type Config struct {
	// NodeID 当前节点ID
	NodeID NodeID

	// ElectionTimeout 选举超时时间
	ElectionTimeout time.Duration

	// HeartbeatInterval 心跳间隔
	HeartbeatInterval time.Duration

	// MaxLogEntries 单次追加的最大日志条目数
	MaxLogEntries int

	// SnapshotThreshold 触发快照的日志条目数阈值
	SnapshotThreshold int

	// Servers 集群服务器列表
	Servers []Server
}

// LoadMetrics 负载指标统计 - 扩展Raft指标系统支持负载均衡
type LoadMetrics struct {
	// QPS 每秒查询数
	QPS float64 `json:"qps"`

	// StorageUsage 存储使用量(MB)
	StorageUsage float64 `json:"storageUsage"`

	// MemoryUsage 内存使用率 (0.0-1.0)
	MemoryUsage float64 `json:"memoryUsage"`

	// NetworkIOBytes 网络IO字节数/秒
	NetworkIOBytes float64 `json:"networkIOBytes"`

	// CPUUsage CPU使用率 (0.0-1.0)
	CPUUsage float64 `json:"cpuUsage"`

	// ActiveConnections 活跃连接数
	ActiveConnections int64 `json:"activeConnections"`

	// PendingOperations 待处理操作数
	PendingOperations int64 `json:"pendingOperations"`

	// LastUpdate 最后更新时间
	LastUpdate time.Time `json:"lastUpdate"`

	// HotKeys 热点键列表(最多10个)
	HotKeys []string `json:"hotKeys"`

	// LoadScore 综合负载评分 (0.0-1.0)
	LoadScore float64 `json:"loadScore"`
}

// Metrics Raft指标统计 - 扩展支持负载监控
type Metrics struct {
	// === 原有Raft指标 ===
	// CurrentTerm 当前任期
	CurrentTerm Term

	// State 当前状态
	State NodeState

	// Leader 当前领导者
	Leader NodeID

	// LastLogIndex 最后日志索引
	LastLogIndex LogIndex

	// CommitIndex 提交索引
	CommitIndex LogIndex

	// LastApplied 最后应用索引
	LastApplied LogIndex

	// === 新增负载指标 ===
	// Load 负载指标
	Load *LoadMetrics `json:"load,omitempty"`

	// CollectedAt 指标收集时间
	CollectedAt time.Time `json:"collectedAt"`
}

// Event 事件类型
type Event interface {
	Type() string
}

// StateChangeEvent 状态变更事件
type StateChangeEvent struct {
	OldState NodeState
	NewState NodeState
	Term     Term
}

func (e *StateChangeEvent) Type() string {
	return "StateChange"
}

// LeaderChangeEvent 领导者变更事件
type LeaderChangeEvent struct {
	OldLeader NodeID
	NewLeader NodeID
	Term      Term
}

func (e *LeaderChangeEvent) Type() string {
	return "LeaderChange"
}

// EventListener 事件监听器
type EventListener interface {
	OnEvent(event Event)
}
