/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Raft consensus server - kv.go
 */
package statemachine

import (
	"encoding/json"
	"fmt"
	"sync"

	"raftserver/raft"
)

// Command 命令类型
type Command struct {
	Type  string      `json:"type"`  // 命令类型: SET, GET, DELETE
	Key   string      `json:"key"`   // 键
	Value interface{} `json:"value"` // 值
}

// KVStateMachine 键值存储状态机
type KVStateMachine struct {
	mu   sync.RWMutex
	data map[string]interface{}
}

// NewKVStateMachine 创建新的键值存储状态机
func NewKVStateMachine() *KVStateMachine {
	return &KVStateMachine{
		data: make(map[string]interface{}),
	}
}

// Apply 应用日志条目到状态机
func (sm *KVStateMachine) Apply(entry *raft.LogEntry) error {
	if entry.Type != raft.EntryNormal {
		// 跳过非普通条目
		return nil
	}

	var cmd Command
	if err := json.Unmarshal(entry.Data, &cmd); err != nil {
		return fmt.Errorf("解析命令失败: %w", err)
	}

	sm.mu.Lock()
	defer sm.mu.Unlock()

	switch cmd.Type {
	case "SET":
		sm.data[cmd.Key] = cmd.Value
	case "DELETE":
		delete(sm.data, cmd.Key)
	case "GET":
		// GET命令不修改状态，通常用于只读操作
		// 在实际实现中，可以考虑不将GET命令加入日志
	default:
		return fmt.Errorf("未知命令类型: %s", cmd.Type)
	}

	return nil
}

// CreateSnapshot 创建状态机快照
func (sm *KVStateMachine) CreateSnapshot() ([]byte, error) {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	snapshot := make(map[string]interface{})
	for k, v := range sm.data {
		snapshot[k] = v
	}

	data, err := json.Marshal(snapshot)
	if err != nil {
		return nil, fmt.Errorf("序列化快照失败: %w", err)
	}

	return data, nil
}

// RestoreSnapshot 从快照恢复状态机
func (sm *KVStateMachine) RestoreSnapshot(data []byte) error {
	var snapshot map[string]interface{}
	if err := json.Unmarshal(data, &snapshot); err != nil {
		return fmt.Errorf("反序列化快照失败: %w", err)
	}

	sm.mu.Lock()
	defer sm.mu.Unlock()

	sm.data = snapshot

	return nil
}

// Get 获取键值
func (sm *KVStateMachine) Get(key string) (interface{}, bool) {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	value, exists := sm.data[key]
	return value, exists
}

// GetAll 获取所有键值对
func (sm *KVStateMachine) GetAll() map[string]interface{} {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	result := make(map[string]interface{})
	for k, v := range sm.data {
		result[k] = v
	}

	return result
}

// Size 获取存储的键值对数量
func (sm *KVStateMachine) Size() int {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	return len(sm.data)
}

// Keys 获取所有键
func (sm *KVStateMachine) Keys() []string {
	sm.mu.RLock()
	defer sm.mu.RUnlock()

	keys := make([]string, 0, len(sm.data))
	for k := range sm.data {
		keys = append(keys, k)
	}

	return keys
}

// CreateSetCommand 创建SET命令
func CreateSetCommand(key string, value interface{}) ([]byte, error) {
	cmd := Command{
		Type:  "SET",
		Key:   key,
		Value: value,
	}

	return json.Marshal(cmd)
}

// CreateDeleteCommand 创建DELETE命令
func CreateDeleteCommand(key string) ([]byte, error) {
	cmd := Command{
		Type: "DELETE",
		Key:  key,
	}

	return json.Marshal(cmd)
}

// CreateGetCommand 创建GET命令
func CreateGetCommand(key string) ([]byte, error) {
	cmd := Command{
		Type: "GET",
		Key:  key,
	}

	return json.Marshal(cmd)
}
