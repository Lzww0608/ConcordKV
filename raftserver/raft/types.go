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

// DataCenterID 数据中心标识符
type DataCenterID string

// ReplicaType 副本类型
type ReplicaType int

const (
	// PrimaryReplica 主副本（同步复制）
	PrimaryReplica ReplicaType = iota
	// AsyncReplica 异步副本（异步复制）
	AsyncReplica
)

func (r ReplicaType) String() string {
	switch r {
	case PrimaryReplica:
		return "Primary"
	case AsyncReplica:
		return "Async"
	default:
		return "Unknown"
	}
}

type Server struct {
	ID          NodeID       `json:"id"`          // 服务器ID
	Address     string       `json:"address"`     // 服务器地址
	DataCenter  DataCenterID `json:"dataCenter"`  // 数据中心标识
	ReplicaType ReplicaType  `json:"replicaType"` // 副本类型
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

// DataCenterConfig 数据中心配置
type DataCenterConfig struct {
	// ID 数据中心标识
	ID DataCenterID `json:"id"`

	// IsPrimary 是否为主数据中心
	IsPrimary bool `json:"isPrimary"`

	// AsyncReplicationDelay 异步复制延迟
	AsyncReplicationDelay time.Duration `json:"asyncReplicationDelay"`

	// MaxAsyncBatchSize 异步复制最大批次大小
	MaxAsyncBatchSize int `json:"maxAsyncBatchSize"`

	// EnableCompression 是否启用压缩传输
	EnableCompression bool `json:"enableCompression"`
}

// MultiDCConfig 多数据中心配置
type MultiDCConfig struct {
	// Enabled 是否启用多数据中心模式
	Enabled bool `json:"enabled"`

	// LocalDataCenter 本地数据中心配置
	LocalDataCenter *DataCenterConfig `json:"localDataCenter"`

	// DataCenters 所有数据中心配置
	DataCenters map[DataCenterID]*DataCenterConfig `json:"dataCenters"`

	// CrossDCHeartbeatInterval 跨数据中心心跳间隔
	CrossDCHeartbeatInterval time.Duration `json:"crossDCHeartbeatInterval"`

	// CrossDCElectionTimeout 跨数据中心选举超时
	CrossDCElectionTimeout time.Duration `json:"crossDCElectionTimeout"`

	// DCPriorityElection 是否启用数据中心优先级选举
	DCPriorityElection bool `json:"dcPriorityElection"`

	// MaxCrossDCLatency 最大跨数据中心延迟容忍度
	MaxCrossDCLatency time.Duration `json:"maxCrossDCLatency"`
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

	// MultiDC 多数据中心配置
	MultiDC *MultiDCConfig `json:"multiDC,omitempty"`
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
}

// Metrics Raft节点指标统计
type Metrics struct {
	// 基础指标
	CurrentTerm Term      `json:"currentTerm"` // 当前任期
	CommitIndex LogIndex  `json:"commitIndex"` // 提交索引
	LastApplied LogIndex  `json:"lastApplied"` // 最后应用索引
	State       NodeState `json:"state"`       // 节点状态
	LeaderID    NodeID    `json:"leaderID"`    // 领导者ID

	// 性能指标
	RequestsPerSecond float64 `json:"requestsPerSecond"` // 每秒请求数
	AverageLatency    float64 `json:"averageLatency"`    // 平均延迟(ms)

	// 选举指标
	ElectionCount    int64 `json:"electionCount"`    // 选举次数
	LastElectionTime int64 `json:"lastElectionTime"` // 最后选举时间

	// 日志指标
	LogEntryCount int64 `json:"logEntryCount"` // 日志条目数

	// 负载指标
	Load LoadMetrics `json:"load"` // 负载指标
}

// EventListener 事件监听器接口
type EventListener interface {
	// OnStateChange 状态变更事件
	OnStateChange(event StateChangeEvent)

	// OnLeaderChange 领导者变更事件
	OnLeaderChange(event LeaderChangeEvent)
}

// StateChangeEvent 状态变更事件
type StateChangeEvent struct {
	NodeID   NodeID    `json:"nodeID"`   // 节点ID
	OldState NodeState `json:"oldState"` // 旧状态
	NewState NodeState `json:"newState"` // 新状态
	Term     Term      `json:"term"`     // 任期
	Time     int64     `json:"time"`     // 事件时间戳
}

// LeaderChangeEvent 领导者变更事件
type LeaderChangeEvent struct {
	NodeID      NodeID `json:"nodeID"`      // 节点ID
	OldLeaderID NodeID `json:"oldLeaderID"` // 旧领导者ID
	NewLeaderID NodeID `json:"newLeaderID"` // 新领导者ID
	Term        Term   `json:"term"`        // 任期
	Time        int64  `json:"time"`        // 事件时间戳
}
