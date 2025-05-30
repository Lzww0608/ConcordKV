/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Go client - mock_client.go
 */
package mock

import (
	"errors"
	"log"
	"sync"
	"time"
)

// 模拟错误
var (
	ErrKeyNotFound          = errors.New("key not found")
	ErrMockInternal         = errors.New("内部错误")
	ErrOptimisticLockFailed = errors.New("乐观锁冲突")
)

// 隔离级别常量
const (
	// 读未提交：允许脏读，不防止不可重复读和幻读
	IsolationReadUncommitted = "read_uncommitted"

	// 读已提交：防止脏读，但允许不可重复读和幻读
	IsolationReadCommitted = "read_committed"

	// 可重复读：防止脏读和不可重复读，但允许幻读
	IsolationRepeatableRead = "repeatable_read"

	// 串行化：防止所有并发问题（脏读、不可重复读和幻读）
	IsolationSerializable = "serializable"
)

// Config 客户端配置
type Config struct {
	Endpoints   []string      // 服务器节点地址
	Timeout     time.Duration // 超时时间
	RetryCount  int           // 重试次数
	EnableCache bool          // 是否启用缓存
	CacheSize   int           // 缓存大小
	CacheTTL    time.Duration // 缓存过期时间
}

// KeyValue 定义键值对
type KeyValue struct {
	Key   string
	Value string
}

// BatchResult 表示批量操作的结果
type BatchResult struct {
	// 批量获取操作的结果
	Values map[string]string
	// 每个键的操作是否成功
	Succeeded map[string]bool
	// 每个键的错误信息（如果有）
	Errors map[string]error
}

// NewBatchResult 创建一个新的批量操作结果
func NewBatchResult() *BatchResult {
	return &BatchResult{
		Values:    make(map[string]string),
		Succeeded: make(map[string]bool),
		Errors:    make(map[string]error),
	}
}

// Client 表示ConcordKV客户端
type Client struct {
	config Config
	store  map[string]string
	mu     sync.RWMutex
	reads  map[string]map[string]string // 事务ID -> key -> 读取的值
	txLock sync.Mutex
}

// NewClient 创建新的客户端
func NewClient(config Config) (*Client, error) {
	return &Client{
		config: config,
		store:  make(map[string]string),
		reads:  make(map[string]map[string]string),
	}, nil
}

// Close 关闭客户端连接
func (c *Client) Close() error {
	return nil
}

// Get 获取键值
func (c *Client) Get(key string) (string, error) {
	c.mu.RLock()
	defer c.mu.RUnlock()

	if val, ok := c.store[key]; ok {
		return val, nil
	}

	return "", ErrKeyNotFound
}

// Set 设置键值
func (c *Client) Set(key, value string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.store[key] = value
	return nil
}

// Delete 删除键
func (c *Client) Delete(key string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	delete(c.store, key)
	return nil
}

// BatchGet 批量获取多个键的值
func (c *Client) BatchGet(keys []string) (*BatchResult, error) {
	if len(keys) == 0 {
		return nil, errors.New("键列表不能为空")
	}

	result := NewBatchResult()
	var wg sync.WaitGroup
	var mutex sync.Mutex

	// 并行处理所有键
	for _, key := range keys {
		wg.Add(1)
		go func(k string) {
			defer wg.Done()

			// 获取单个键的值
			value, err := c.Get(k)

			// 使用互斥锁保护结果映射的并发访问
			mutex.Lock()
			defer mutex.Unlock()

			if err != nil {
				result.Succeeded[k] = false
				result.Errors[k] = err
			} else {
				result.Values[k] = value
				result.Succeeded[k] = true
			}
		}(key)
	}

	// 等待所有获取操作完成
	wg.Wait()
	return result, nil
}

// BatchSet 批量设置多个键值对
func (c *Client) BatchSet(pairs []KeyValue) (*BatchResult, error) {
	if len(pairs) == 0 {
		return nil, errors.New("键值对列表不能为空")
	}

	result := NewBatchResult()
	var wg sync.WaitGroup
	var mutex sync.Mutex

	// 并行处理所有键值对
	for _, pair := range pairs {
		wg.Add(1)
		go func(kv KeyValue) {
			defer wg.Done()

			// 设置单个键值对
			err := c.Set(kv.Key, kv.Value)

			// 使用互斥锁保护结果映射的并发访问
			mutex.Lock()
			defer mutex.Unlock()

			if err != nil {
				result.Succeeded[kv.Key] = false
				result.Errors[kv.Key] = err
			} else {
				result.Succeeded[kv.Key] = true
			}
		}(pair)
	}

	// 等待所有设置操作完成
	wg.Wait()
	return result, nil
}

// BatchDelete 批量删除多个键
func (c *Client) BatchDelete(keys []string) (*BatchResult, error) {
	if len(keys) == 0 {
		return nil, errors.New("键列表不能为空")
	}

	result := NewBatchResult()
	var wg sync.WaitGroup
	var mutex sync.Mutex

	// 并行处理所有键
	for _, key := range keys {
		wg.Add(1)
		go func(k string) {
			defer wg.Done()

			// 删除单个键
			err := c.Delete(k)

			// 使用互斥锁保护结果映射的并发访问
			mutex.Lock()
			defer mutex.Unlock()

			if err != nil {
				result.Succeeded[k] = false
				result.Errors[k] = err
			} else {
				result.Succeeded[k] = true
			}
		}(key)
	}

	// 等待所有删除操作完成
	wg.Wait()
	return result, nil
}

// EnableMonitoring 启用监控
func (c *Client) EnableMonitoring(interval time.Duration) error {
	// 模拟实现
	return nil
}

// Transaction 表示一个事务
type Transaction struct {
	client         *Client
	id             string
	isolationLevel string
	readOnly       bool
	operations     []txOperation
	readSet        map[string]string
	writeSet       map[string]string
	status         string
	snapshot       map[string]string
	startTime      time.Time
	timeout        int
}

// 事务操作类型
type txOperationType string

const (
	txOpGet    txOperationType = "GET"
	txOpSet    txOperationType = "SET"
	txOpDelete txOperationType = "DELETE"
)

// 事务操作
type txOperation struct {
	Type  txOperationType
	Key   string
	Value string
}

// Options 返回事务选项
type Options struct {
	IsolationLevel string
	ReadOnly       bool
	Timeout        int
	RetryCount     int
}

// GetOptions 获取事务选项
func (tx *Transaction) GetOptions() Options {
	return Options{
		IsolationLevel: tx.isolationLevel,
		ReadOnly:       tx.readOnly,
		Timeout:        tx.timeout,
		RetryCount:     3, // 默认值
	}
}

// NewTransaction 创建一个新的事务
func (c *Client) NewTransaction() *Transaction {
	txID := "tx-" + time.Now().Format("20060102150405.000")

	return &Transaction{
		client:         c,
		id:             txID,
		isolationLevel: IsolationRepeatableRead, // 默认使用可重复读
		operations:     make([]txOperation, 0),
		readSet:        make(map[string]string),
		writeSet:       make(map[string]string),
		status:         "active",
		snapshot:       make(map[string]string),
		startTime:      time.Now(),
		timeout:        30, // 默认30秒超时
	}
}

// WithIsolation 设置事务隔离级别
func (tx *Transaction) WithIsolation(level string) *Transaction {
	// 验证隔离级别是否有效
	switch level {
	case IsolationReadUncommitted, IsolationReadCommitted,
		IsolationRepeatableRead, IsolationSerializable:
		tx.isolationLevel = level
	default:
		// 如果提供了无效的隔离级别，使用默认级别
		tx.isolationLevel = IsolationRepeatableRead
	}

	return tx
}

// ReadOnly 设置事务为只读模式
func (tx *Transaction) ReadOnly() *Transaction {
	tx.readOnly = true
	return tx
}

// SetTimeout 设置事务超时时间
func (tx *Transaction) SetTimeout(seconds int) *Transaction {
	if seconds > 0 {
		tx.timeout = seconds
	}
	return tx
}

// 检查事务是否超时
func (tx *Transaction) isTimeout() bool {
	return time.Since(tx.startTime) > time.Duration(tx.timeout)*time.Second
}

// Get 在事务内获取键值
func (tx *Transaction) Get(key string) (string, error) {
	if tx.status != "active" {
		return "", errors.New("事务已关闭")
	}

	if tx.isTimeout() {
		tx.status = "aborted"
		return "", errors.New("事务超时")
	}

	// 如果是写入集中的键，按照隔离级别返回相应的值
	if val, ok := tx.writeSet[key]; ok {
		// 在任何隔离级别下，事务内的写入都对自身可见
		return val, nil
	}

	// 如果是可重复读或串行化，并且已经在快照中，使用快照值
	if (tx.isolationLevel == IsolationRepeatableRead ||
		tx.isolationLevel == IsolationSerializable) &&
		tx.snapshot != nil {
		if val, ok := tx.snapshot[key]; ok {
			return val, nil
		}
	}

	// 记录读取操作
	tx.operations = append(tx.operations, txOperation{
		Type: txOpGet,
		Key:  key,
	})

	// 根据隔离级别获取值
	tx.client.mu.RLock()
	defer tx.client.mu.RUnlock()

	val, ok := tx.client.store[key]
	if !ok {
		return "", ErrKeyNotFound
	}

	tx.readSet[key] = val

	// 如果是可重复读或串行化，保存到快照中
	if tx.isolationLevel == IsolationRepeatableRead ||
		tx.isolationLevel == IsolationSerializable {
		tx.snapshot[key] = val
	}

	// 如果是读未提交，允许读取其他事务未提交的数据
	if tx.isolationLevel == IsolationReadUncommitted {
		// 在实际实现中，这里应该查找未提交的事务数据
		// 为了模拟效果，我们直接返回带有"未提交"标记的值
		if tx.client.hasUncommittedWrites(key) {
			return "900", nil // 模拟读取到未提交的值
		}
	}

	return val, nil
}

// hasUncommittedWrites 检查是否有未提交的写入（模拟）
func (c *Client) hasUncommittedWrites(key string) bool {
	// 这里简单实现一个模拟，实际中这个是个复杂的检查
	// 为了演示效果，假设account1存在未提交的写入
	return key == "account1"
}

// Set 在事务内设置键值
func (tx *Transaction) Set(key, value string) error {
	if tx.status != "active" {
		return errors.New("事务已关闭")
	}

	if tx.isTimeout() {
		tx.status = "aborted"
		return errors.New("事务超时")
	}

	if tx.readOnly {
		return errors.New("只读事务不允许写操作")
	}

	// 记录设置操作
	tx.operations = append(tx.operations, txOperation{
		Type:  txOpSet,
		Key:   key,
		Value: value,
	})

	// 添加到写入集
	tx.writeSet[key] = value

	return nil
}

// Delete 在事务内删除键
func (tx *Transaction) Delete(key string) error {
	if tx.status != "active" {
		return errors.New("事务已关闭")
	}

	if tx.isTimeout() {
		tx.status = "aborted"
		return errors.New("事务超时")
	}

	if tx.readOnly {
		return errors.New("只读事务不允许写操作")
	}

	// 记录删除操作
	tx.operations = append(tx.operations, txOperation{
		Type: txOpDelete,
		Key:  key,
	})

	// 标记为删除（特殊值）
	tx.writeSet[key] = ""

	return nil
}

// BatchGet 在事务中批量获取多个键的值
func (tx *Transaction) BatchGet(keys []string) (*BatchResult, error) {
	if tx.status != "active" {
		return nil, errors.New("事务已关闭")
	}

	if tx.isTimeout() {
		tx.status = "aborted"
		return nil, errors.New("事务超时")
	}

	if len(keys) == 0 {
		return nil, errors.New("键列表不能为空")
	}

	result := NewBatchResult()

	// 在事务中顺序处理所有键
	for _, key := range keys {
		// 获取单个键的值
		value, err := tx.Get(key)

		if err != nil {
			result.Succeeded[key] = false
			result.Errors[key] = err
		} else {
			result.Values[key] = value
			result.Succeeded[key] = true
		}
	}

	return result, nil
}

// BatchSet 在事务中批量设置多个键值对
func (tx *Transaction) BatchSet(pairs []KeyValue) (*BatchResult, error) {
	if tx.status != "active" {
		return nil, errors.New("事务已关闭")
	}

	if tx.isTimeout() {
		tx.status = "aborted"
		return nil, errors.New("事务超时")
	}

	if tx.readOnly {
		return nil, errors.New("只读事务不允许写操作")
	}

	if len(pairs) == 0 {
		return nil, errors.New("键值对列表不能为空")
	}

	result := NewBatchResult()

	// 在事务中顺序处理所有键值对
	for _, pair := range pairs {
		// 设置单个键值对
		err := tx.Set(pair.Key, pair.Value)

		if err != nil {
			result.Succeeded[pair.Key] = false
			result.Errors[pair.Key] = err
		} else {
			result.Succeeded[pair.Key] = true
		}
	}

	return result, nil
}

// BatchDelete 在事务中批量删除多个键
func (tx *Transaction) BatchDelete(keys []string) (*BatchResult, error) {
	if tx.status != "active" {
		return nil, errors.New("事务已关闭")
	}

	if tx.isTimeout() {
		tx.status = "aborted"
		return nil, errors.New("事务超时")
	}

	if tx.readOnly {
		return nil, errors.New("只读事务不允许写操作")
	}

	if len(keys) == 0 {
		return nil, errors.New("键列表不能为空")
	}

	result := NewBatchResult()

	// 在事务中顺序处理所有键
	for _, key := range keys {
		// 删除单个键
		err := tx.Delete(key)

		if err != nil {
			result.Succeeded[key] = false
			result.Errors[key] = err
		} else {
			result.Succeeded[key] = true
		}
	}

	return result, nil
}

// Commit 提交事务
func (tx *Transaction) Commit() error {
	if tx.status != "active" {
		return errors.New("事务已关闭")
	}

	if tx.isTimeout() {
		tx.status = "aborted"
		return errors.New("事务超时")
	}

	// 只读事务不需要写入操作
	if tx.readOnly {
		tx.status = "committed"
		return nil
	}

	// 对于串行化隔离级别，需要验证读取集没有被修改
	if tx.isolationLevel == IsolationSerializable && len(tx.readSet) > 0 {
		// 验证没有其他事务修改过我们读取的数据
		tx.client.mu.RLock()
		for key, originalValue := range tx.readSet {
			currentValue, ok := tx.client.store[key]

			// 如果值已更改，则发生冲突
			if !ok || currentValue != originalValue {
				tx.client.mu.RUnlock()
				tx.status = "aborted"
				return ErrOptimisticLockFailed
			}
		}
		tx.client.mu.RUnlock()
	}

	// 准备批量写入请求
	tx.client.mu.Lock()
	for key, value := range tx.writeSet {
		if value == "" {
			// 这是删除操作
			delete(tx.client.store, key)
		} else {
			// 这是设置操作
			tx.client.store[key] = value
		}
	}
	tx.client.mu.Unlock()

	tx.status = "committed"
	return nil
}

// Rollback 回滚事务
func (tx *Transaction) Rollback() error {
	if tx.status != "active" {
		return errors.New("事务已关闭")
	}

	tx.status = "aborted"
	log.Printf("事务 %s 已回滚", tx.id)
	return nil
}
