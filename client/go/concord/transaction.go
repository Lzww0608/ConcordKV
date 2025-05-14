package concord

import (
	"errors"
	"sync"
)

// 事务错误定义
var (
	ErrTxnAlreadyCommitted = errors.New("事务已提交")
	ErrTxnAlreadyAborted   = errors.New("事务已中止")
	ErrTxnConflict         = errors.New("事务冲突")
)

// Operation 事务操作类型
type Operation string

const (
	// 事务操作类型
	OpGet    Operation = "GET"
	OpSet    Operation = "SET"
	OpDelete Operation = "DELETE"
)

// TxnOp 表示事务中的单个操作
type TxnOp struct {
	Type  Operation
	Key   string
	Value string
}

// Transaction 表示一个事务
type Transaction struct {
	client     *Client
	id         string
	operations []TxnOp
	mu         sync.Mutex
	committed  bool
	aborted    bool
}

// NewTransaction 创建新的事务
func (c *Client) NewTransaction() *Transaction {
	return &Transaction{
		client:     c,
		id:         generateTxnID(), // TODO: 实现实际ID生成
		operations: make([]TxnOp, 0),
	}
}

// 生成事务ID
func generateTxnID() string {
	// TODO: 实现事务ID生成逻辑
	return "txn-temp-id"
}

// Get 在事务中获取键值
func (t *Transaction) Get(key string) (string, error) {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.committed {
		return "", ErrTxnAlreadyCommitted
	}
	if t.aborted {
		return "", ErrTxnAlreadyAborted
	}

	// 添加到操作列表
	t.operations = append(t.operations, TxnOp{
		Type: OpGet,
		Key:  key,
	})

	// TODO: 实现实际事务读取逻辑
	return "", ErrKeyNotFound
}

// Set 在事务中设置键值
func (t *Transaction) Set(key, value string) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.committed {
		return ErrTxnAlreadyCommitted
	}
	if t.aborted {
		return ErrTxnAlreadyAborted
	}

	// 添加到操作列表
	t.operations = append(t.operations, TxnOp{
		Type:  OpSet,
		Key:   key,
		Value: value,
	})

	return nil
}

// Delete 在事务中删除键
func (t *Transaction) Delete(key string) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.committed {
		return ErrTxnAlreadyCommitted
	}
	if t.aborted {
		return ErrTxnAlreadyAborted
	}

	// 添加到操作列表
	t.operations = append(t.operations, TxnOp{
		Type: OpDelete,
		Key:  key,
	})

	return nil
}

// Commit 提交事务
func (t *Transaction) Commit() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.committed {
		return ErrTxnAlreadyCommitted
	}
	if t.aborted {
		return ErrTxnAlreadyAborted
	}

	// TODO: 实现实际事务提交逻辑
	t.committed = true
	return nil
}

// Abort 中止事务
func (t *Transaction) Abort() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.committed {
		return ErrTxnAlreadyCommitted
	}
	if t.aborted {
		return ErrTxnAlreadyAborted
	}

	// TODO: 实现实际事务中止逻辑
	t.aborted = true
	return nil
}
