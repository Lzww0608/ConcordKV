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

// Get 在事务内获取键值
func (tx *Transaction) Get(key string) (string, error) {
	if tx.status != "active" {
		return "", errors.New("事务已关闭")
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

// Commit 提交事务
func (tx *Transaction) Commit() error {
	if tx.status != "active" {
		return errors.New("事务已关闭")
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
