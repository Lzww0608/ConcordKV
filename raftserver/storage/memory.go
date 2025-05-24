package storage

import (
	"encoding/json"
	"fmt"
	"sync"

	"raftserver/raft"
)

// MemoryStorage 内存存储实现
type MemoryStorage struct {
	mu            sync.RWMutex
	currentTerm   raft.Term
	votedFor      raft.NodeID
	logs          []raft.LogEntry
	snapshot      *raft.Snapshot
	firstLogIndex raft.LogIndex
}

// NewMemoryStorage 创建新的内存存储
func NewMemoryStorage() *MemoryStorage {
	return &MemoryStorage{
		currentTerm:   0,
		votedFor:      "",
		logs:          make([]raft.LogEntry, 0),
		firstLogIndex: 1,
	}
}

// SaveCurrentTerm 保存当前任期号
func (s *MemoryStorage) SaveCurrentTerm(term raft.Term) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.currentTerm = term
	return nil
}

// GetCurrentTerm 获取当前任期号
func (s *MemoryStorage) GetCurrentTerm() (raft.Term, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.currentTerm, nil
}

// SaveVotedFor 保存投票给的候选人
func (s *MemoryStorage) SaveVotedFor(candidateID raft.NodeID) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.votedFor = candidateID
	return nil
}

// GetVotedFor 获取投票给的候选人
func (s *MemoryStorage) GetVotedFor() (raft.NodeID, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.votedFor, nil
}

// SaveLogEntries 保存日志条目
func (s *MemoryStorage) SaveLogEntries(entries []raft.LogEntry) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	for _, entry := range entries {
		// 计算在数组中的位置
		arrayIndex := entry.Index - s.firstLogIndex

		// 如果需要扩展数组
		if arrayIndex >= raft.LogIndex(len(s.logs)) {
			// 扩展数组到需要的大小
			newLogs := make([]raft.LogEntry, arrayIndex+1)
			copy(newLogs, s.logs)
			s.logs = newLogs
		}

		s.logs[arrayIndex] = entry
	}

	return nil
}

// GetLogEntry 获取指定索引的日志条目
func (s *MemoryStorage) GetLogEntry(index raft.LogIndex) (*raft.LogEntry, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	// 检查是否在快照范围内
	if s.snapshot != nil && index <= s.snapshot.LastIncludedIndex {
		return nil, fmt.Errorf("日志条目 %d 已被快照包含", index)
	}

	// 检查索引是否有效
	if index < s.firstLogIndex {
		return nil, fmt.Errorf("日志索引 %d 小于第一个日志索引 %d", index, s.firstLogIndex)
	}

	arrayIndex := index - s.firstLogIndex
	if arrayIndex >= raft.LogIndex(len(s.logs)) {
		return nil, fmt.Errorf("日志索引 %d 超出范围", index)
	}

	entry := s.logs[arrayIndex]
	if entry.Index == 0 {
		return nil, fmt.Errorf("日志条目 %d 不存在", index)
	}

	return &entry, nil
}

// GetLogEntries 获取指定范围的日志条目
func (s *MemoryStorage) GetLogEntries(start, end raft.LogIndex) ([]raft.LogEntry, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	if start > end {
		return nil, fmt.Errorf("起始索引 %d 大于结束索引 %d", start, end)
	}

	var entries []raft.LogEntry
	for i := start; i <= end; i++ {
		arrayIndex := i - s.firstLogIndex
		if arrayIndex >= 0 && arrayIndex < raft.LogIndex(len(s.logs)) {
			entry := s.logs[arrayIndex]
			if entry.Index != 0 {
				entries = append(entries, entry)
			}
		}
	}

	return entries, nil
}

// GetLastLogIndex 获取最后一个日志索引
func (s *MemoryStorage) GetLastLogIndex() raft.LogIndex {
	s.mu.RLock()
	defer s.mu.RUnlock()

	// 如果有快照且没有日志，返回快照的最后索引
	if len(s.logs) == 0 {
		if s.snapshot != nil {
			return s.snapshot.LastIncludedIndex
		}
		return 0
	}

	// 从后往前查找最后一个有效的日志条目
	for i := len(s.logs) - 1; i >= 0; i-- {
		if s.logs[i].Index != 0 {
			return s.logs[i].Index
		}
	}

	// 如果没有有效日志，返回快照的最后索引
	if s.snapshot != nil {
		return s.snapshot.LastIncludedIndex
	}

	return 0
}

// GetLastLogTerm 获取最后一个日志的任期号
func (s *MemoryStorage) GetLastLogTerm() raft.Term {
	s.mu.RLock()
	defer s.mu.RUnlock()

	// 如果有日志，返回最后一个日志的任期
	if len(s.logs) > 0 {
		for i := len(s.logs) - 1; i >= 0; i-- {
			if s.logs[i].Index != 0 {
				return s.logs[i].Term
			}
		}
	}

	// 如果没有日志但有快照，返回快照的任期
	if s.snapshot != nil {
		return s.snapshot.LastIncludedTerm
	}

	return 0
}

// TruncateLog 截断日志（删除指定索引之后的所有条目）
func (s *MemoryStorage) TruncateLog(index raft.LogIndex) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if index < s.firstLogIndex {
		// 清空所有日志
		s.logs = make([]raft.LogEntry, 0)
		return nil
	}

	arrayIndex := index - s.firstLogIndex + 1
	if arrayIndex <= raft.LogIndex(len(s.logs)) {
		// 截断到指定位置
		s.logs = s.logs[:arrayIndex]
	}

	return nil
}

// SaveSnapshot 保存快照
func (s *MemoryStorage) SaveSnapshot(snapshot *raft.Snapshot) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.snapshot = snapshot

	// 更新第一个日志索引
	s.firstLogIndex = snapshot.LastIncludedIndex + 1

	// 删除快照覆盖的日志条目
	newLogs := make([]raft.LogEntry, 0)
	for _, entry := range s.logs {
		if entry.Index > snapshot.LastIncludedIndex {
			newLogs = append(newLogs, entry)
		}
	}
	s.logs = newLogs

	return nil
}

// GetSnapshot 获取快照
func (s *MemoryStorage) GetSnapshot() (*raft.Snapshot, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	if s.snapshot == nil {
		return nil, fmt.Errorf("快照不存在")
	}

	return s.snapshot, nil
}

// Close 关闭存储
func (s *MemoryStorage) Close() error {
	// 内存存储没有需要关闭的资源
	return nil
}

// GetLogStats 获取日志统计信息（用于调试）
func (s *MemoryStorage) GetLogStats() map[string]interface{} {
	s.mu.RLock()
	defer s.mu.RUnlock()

	stats := map[string]interface{}{
		"currentTerm":   s.currentTerm,
		"votedFor":      s.votedFor,
		"firstLogIndex": s.firstLogIndex,
		"logCount":      len(s.logs),
		"lastLogIndex":  s.GetLastLogIndex(),
		"lastLogTerm":   s.GetLastLogTerm(),
		"hasSnapshot":   s.snapshot != nil,
	}

	if s.snapshot != nil {
		stats["snapshotLastIndex"] = s.snapshot.LastIncludedIndex
		stats["snapshotLastTerm"] = s.snapshot.LastIncludedTerm
	}

	return stats
}

// DebugLogs 获取所有日志（用于调试）
func (s *MemoryStorage) DebugLogs() string {
	s.mu.RLock()
	defer s.mu.RUnlock()

	data, _ := json.MarshalIndent(map[string]interface{}{
		"stats": s.GetLogStats(),
		"logs":  s.logs,
	}, "", "  ")

	return string(data)
}
