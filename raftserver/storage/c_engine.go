package storage

/*
#cgo CFLAGS: -I../../kvserver
#cgo LDFLAGS: -L../../kvserver -lkvstore

#include <stdlib.h>
#include <string.h>
#include "kvstore.h"

// 包装函数，用于处理Go字符串
int c_kvstore_set_wrapper(void* store, const char* key, size_t key_len, const char* value, size_t value_len) {
    return kvstore_set(store, key, key_len, value, value_len);
}

int c_kvstore_get_wrapper(void* store, const char* key, size_t key_len, char** value, size_t* value_len) {
    return kvstore_get(store, key, key_len, value, value_len);
}

int c_kvstore_delete_wrapper(void* store, const char* key, size_t key_len) {
    return kvstore_delete(store, key, key_len);
}
*/
import "C"

import (
	"fmt"
	"sync"
	"unsafe"

	"raftserver/raft"
)

// CEngineStorage C引擎存储实现
type CEngineStorage struct {
	mu            sync.RWMutex
	store         unsafe.Pointer
	currentTerm   raft.Term
	votedFor      raft.NodeID
	snapshot      *raft.Snapshot
	firstLogIndex raft.LogIndex
	engineType    string
}

// NewCEngineStorage 创建新的C引擎存储
func NewCEngineStorage(engineType string) (*CEngineStorage, error) {
	var store unsafe.Pointer

	switch engineType {
	case "array":
		store = C.kvstore_create_array()
	case "rbtree":
		store = C.kvstore_create_rbtree()
	case "hash":
		store = C.kvstore_create_hash()
	default:
		return nil, fmt.Errorf("不支持的引擎类型: %s", engineType)
	}

	if store == nil {
		return nil, fmt.Errorf("创建C引擎失败")
	}

	return &CEngineStorage{
		store:         store,
		currentTerm:   0,
		votedFor:      "",
		firstLogIndex: 1,
		engineType:    engineType,
	}, nil
}

// SaveCurrentTerm 保存当前任期号
func (s *CEngineStorage) SaveCurrentTerm(term raft.Term) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	key := "raft_current_term"
	value := fmt.Sprintf("%d", term)

	cKey := C.CString(key)
	cValue := C.CString(value)
	defer C.free(unsafe.Pointer(cKey))
	defer C.free(unsafe.Pointer(cValue))

	result := C.c_kvstore_set_wrapper(s.store, cKey, C.size_t(len(key)), cValue, C.size_t(len(value)))
	if result != 0 {
		return fmt.Errorf("保存当前任期失败")
	}

	s.currentTerm = term
	return nil
}

// GetCurrentTerm 获取当前任期号
func (s *CEngineStorage) GetCurrentTerm() (raft.Term, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	key := "raft_current_term"
	cKey := C.CString(key)
	defer C.free(unsafe.Pointer(cKey))

	var cValue *C.char
	var valueLen C.size_t

	result := C.c_kvstore_get_wrapper(s.store, cKey, C.size_t(len(key)), &cValue, &valueLen)
	if result != 0 {
		// 如果没有找到，返回0
		return 0, nil
	}

	if cValue != nil {
		value := C.GoStringN(cValue, C.int(valueLen))
		C.free(unsafe.Pointer(cValue))

		var term raft.Term
		if _, err := fmt.Sscanf(value, "%d", &term); err != nil {
			return 0, err
		}
		s.currentTerm = term
		return term, nil
	}

	return 0, nil
}

// SaveVotedFor 保存投票给的候选人
func (s *CEngineStorage) SaveVotedFor(candidateID raft.NodeID) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	key := "raft_voted_for"
	value := string(candidateID)

	cKey := C.CString(key)
	cValue := C.CString(value)
	defer C.free(unsafe.Pointer(cKey))
	defer C.free(unsafe.Pointer(cValue))

	result := C.c_kvstore_set_wrapper(s.store, cKey, C.size_t(len(key)), cValue, C.size_t(len(value)))
	if result != 0 {
		return fmt.Errorf("保存投票状态失败")
	}

	s.votedFor = candidateID
	return nil
}

// GetVotedFor 获取投票给的候选人
func (s *CEngineStorage) GetVotedFor() (raft.NodeID, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	key := "raft_voted_for"
	cKey := C.CString(key)
	defer C.free(unsafe.Pointer(cKey))

	var cValue *C.char
	var valueLen C.size_t

	result := C.c_kvstore_get_wrapper(s.store, cKey, C.size_t(len(key)), &cValue, &valueLen)
	if result != 0 {
		// 如果没有找到，返回空字符串
		return "", nil
	}

	if cValue != nil {
		value := C.GoStringN(cValue, C.int(valueLen))
		C.free(unsafe.Pointer(cValue))
		s.votedFor = raft.NodeID(value)
		return raft.NodeID(value), nil
	}

	return "", nil
}

// SaveLogEntries 保存日志条目
func (s *CEngineStorage) SaveLogEntries(entries []raft.LogEntry) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	for _, entry := range entries {
		key := fmt.Sprintf("raft_log_%d", entry.Index)
		value := s.serializeLogEntry(entry)

		cKey := C.CString(key)
		cValue := C.CString(value)
		defer C.free(unsafe.Pointer(cKey))
		defer C.free(unsafe.Pointer(cValue))

		result := C.c_kvstore_set_wrapper(s.store, cKey, C.size_t(len(key)), cValue, C.size_t(len(value)))
		if result != 0 {
			return fmt.Errorf("保存日志条目 %d 失败", entry.Index)
		}
	}

	return nil
}

// GetLogEntry 获取指定索引的日志条目
func (s *CEngineStorage) GetLogEntry(index raft.LogIndex) (*raft.LogEntry, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	key := fmt.Sprintf("raft_log_%d", index)
	cKey := C.CString(key)
	defer C.free(unsafe.Pointer(cKey))

	var cValue *C.char
	var valueLen C.size_t

	result := C.c_kvstore_get_wrapper(s.store, cKey, C.size_t(len(key)), &cValue, &valueLen)
	if result != 0 {
		return nil, fmt.Errorf("日志条目 %d 不存在", index)
	}

	if cValue != nil {
		value := C.GoStringN(cValue, C.int(valueLen))
		C.free(unsafe.Pointer(cValue))

		entry, err := s.deserializeLogEntry(value)
		if err != nil {
			return nil, err
		}
		return entry, nil
	}

	return nil, fmt.Errorf("日志条目 %d 不存在", index)
}

// GetLogEntries 获取指定范围的日志条目
func (s *CEngineStorage) GetLogEntries(start, end raft.LogIndex) ([]raft.LogEntry, error) {
	var entries []raft.LogEntry

	for i := start; i <= end; i++ {
		entry, err := s.GetLogEntry(i)
		if err != nil {
			continue // 跳过不存在的条目
		}
		entries = append(entries, *entry)
	}

	return entries, nil
}

// GetLastLogIndex 获取最后一个日志索引
func (s *CEngineStorage) GetLastLogIndex() raft.LogIndex {
	s.mu.RLock()
	defer s.mu.RUnlock()

	// 简化实现：从大到小搜索存在的日志条目
	for i := raft.LogIndex(10000); i >= 1; i-- {
		key := fmt.Sprintf("raft_log_%d", i)
		cKey := C.CString(key)

		var cValue *C.char
		var valueLen C.size_t

		result := C.c_kvstore_get_wrapper(s.store, cKey, C.size_t(len(key)), &cValue, &valueLen)
		C.free(unsafe.Pointer(cKey))

		if result == 0 && cValue != nil {
			C.free(unsafe.Pointer(cValue))
			return i
		}
	}

	return 0
}

// GetLastLogTerm 获取最后一个日志的任期号
func (s *CEngineStorage) GetLastLogTerm() raft.Term {
	lastIndex := s.GetLastLogIndex()
	if lastIndex == 0 {
		return 0
	}

	entry, err := s.GetLogEntry(lastIndex)
	if err != nil {
		return 0
	}

	return entry.Term
}

// TruncateLog 截断日志（删除指定索引之后的所有条目）
func (s *CEngineStorage) TruncateLog(index raft.LogIndex) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	// 删除index+1之后的所有日志条目
	for i := index + 1; i <= s.GetLastLogIndex()+100; i++ {
		key := fmt.Sprintf("raft_log_%d", i)
		cKey := C.CString(key)

		C.c_kvstore_delete_wrapper(s.store, cKey, C.size_t(len(key)))
		C.free(unsafe.Pointer(cKey))
	}

	return nil
}

// SaveSnapshot 保存快照
func (s *CEngineStorage) SaveSnapshot(snapshot *raft.Snapshot) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.snapshot = snapshot

	key := "raft_snapshot"
	value := s.serializeSnapshot(snapshot)

	cKey := C.CString(key)
	cValue := C.CString(value)
	defer C.free(unsafe.Pointer(cKey))
	defer C.free(unsafe.Pointer(cValue))

	result := C.c_kvstore_set_wrapper(s.store, cKey, C.size_t(len(key)), cValue, C.size_t(len(value)))
	if result != 0 {
		return fmt.Errorf("保存快照失败")
	}

	return nil
}

// GetSnapshot 获取快照
func (s *CEngineStorage) GetSnapshot() (*raft.Snapshot, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	if s.snapshot != nil {
		return s.snapshot, nil
	}

	key := "raft_snapshot"
	cKey := C.CString(key)
	defer C.free(unsafe.Pointer(cKey))

	var cValue *C.char
	var valueLen C.size_t

	result := C.c_kvstore_get_wrapper(s.store, cKey, C.size_t(len(key)), &cValue, &valueLen)
	if result != 0 {
		return nil, fmt.Errorf("快照不存在")
	}

	if cValue != nil {
		value := C.GoStringN(cValue, C.int(valueLen))
		C.free(unsafe.Pointer(cValue))

		snapshot, err := s.deserializeSnapshot(value)
		if err != nil {
			return nil, err
		}
		s.snapshot = snapshot
		return snapshot, nil
	}

	return nil, fmt.Errorf("快照不存在")
}

// Close 关闭存储
func (s *CEngineStorage) Close() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.store != nil {
		C.kvstore_destroy(s.store)
		s.store = nil
	}

	return nil
}

// 辅助函数：序列化日志条目
func (s *CEngineStorage) serializeLogEntry(entry raft.LogEntry) string {
	return fmt.Sprintf("%d|%d|%d|%d|%s",
		entry.Index, entry.Term, entry.Type, entry.Timestamp.Unix(), string(entry.Data))
}

// 辅助函数：反序列化日志条目
func (s *CEngineStorage) deserializeLogEntry(data string) (*raft.LogEntry, error) {
	// 简化的反序列化实现
	// 实际应该使用更健壮的序列化格式如JSON或protobuf
	return nil, fmt.Errorf("反序列化未实现")
}

// 辅助函数：序列化快照
func (s *CEngineStorage) serializeSnapshot(snapshot *raft.Snapshot) string {
	return fmt.Sprintf("%d|%d|%s",
		snapshot.LastIncludedIndex, snapshot.LastIncludedTerm, string(snapshot.Data))
}

// 辅助函数：反序列化快照
func (s *CEngineStorage) deserializeSnapshot(data string) (*raft.Snapshot, error) {
	// 简化的反序列化实现
	return nil, fmt.Errorf("反序列化未实现")
}
