package concord

import (
	"encoding/json"
	"errors"
	"sync"
	"time"
)

// 事务错误定义
var (
	ErrTxnAlreadyCommitted = errors.New("事务已提交")
	ErrTxnAlreadyAborted   = errors.New("事务已中止")
	ErrTxnConflict         = errors.New("事务冲突")
	ErrTxnTimeout          = errors.New("事务超时")
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
	results    map[string]string // 缓存读取结果
}

// NewTransaction 创建新的事务
func (c *Client) NewTransaction() *Transaction {
	return &Transaction{
		client:     c,
		id:         generateTxnID(),
		operations: make([]TxnOp, 0),
		results:    make(map[string]string),
	}
}

// 生成事务ID
func generateTxnID() string {
	return time.Now().Format("20060102150405") + "-txn"
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

	// 发送读取请求到服务器
	req := request{
		Type:  "TXN_GET",
		Key:   key,
		Value: t.id,
	}

	resp, err := t.client.doRequestWithRetry(req)
	if err != nil {
		return "", err
	}

	if !resp.Success {
		if resp.Error == "key not found" {
			return "", ErrKeyNotFound
		}
		return "", errors.New(resp.Error)
	}

	// 缓存结果
	t.results[key] = resp.Value
	return resp.Value, nil
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

	// 缓存结果
	t.results[key] = value

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

	// 从缓存中删除
	delete(t.results, key)

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

	// 如果没有操作，直接返回成功
	if len(t.operations) == 0 {
		t.committed = true
		return nil
	}

	// 构建事务提交请求
	txnData, err := json.Marshal(struct {
		ID         string  `json:"id"`
		Operations []TxnOp `json:"operations"`
	}{
		ID:         t.id,
		Operations: t.operations,
	})
	if err != nil {
		return err
	}

	// 发送提交请求
	req := request{
		Type:  "TXN_COMMIT",
		Value: string(txnData),
	}

	resp, err := t.client.doRequestWithRetry(req)
	if err != nil {
		return err
	}

	if !resp.Success {
		if resp.Error == "transaction conflict" {
			return ErrTxnConflict
		}
		return errors.New(resp.Error)
	}

	// 更新客户端缓存
	if t.client.cache != nil {
		for _, op := range t.operations {
			switch op.Type {
			case OpSet:
				t.client.cache.Set(op.Key, op.Value, t.client.config.CacheTTL)
			case OpDelete:
				t.client.cache.Delete(op.Key)
			}
		}
	}

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

	// 发送中止请求
	if len(t.operations) > 0 {
		req := request{
			Type:  "TXN_ABORT",
			Value: t.id,
		}

		_, _ = t.client.doRequestWithRetry(req)
	}

	t.aborted = true
	return nil
}
