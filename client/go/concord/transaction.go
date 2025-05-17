package concord

// Transaction 表示一个事务
type Transaction struct {
	options TransactionOptions
	status  string
}

// WithIsolation 设置事务隔离级别
func (t *Transaction) WithIsolation(level string) *Transaction {
	t.options.IsolationLevel = level
	return t
}

// ReadOnly 设置事务为只读模式
func (t *Transaction) ReadOnly() *Transaction {
	t.options.ReadOnly = true
	return t
}

// SetTimeout 设置事务超时时间
func (t *Transaction) SetTimeout(seconds int) *Transaction {
	if seconds > 0 {
		t.options.Timeout = seconds
	}
	return t
}

// Get 在事务中获取键值
func (t *Transaction) Get(key string) (string, error) {
	return "", nil
}

// Set 在事务中设置键值
func (t *Transaction) Set(key, value string) error {
	return nil
}

// Delete 在事务中删除键
func (t *Transaction) Delete(key string) error {
	return nil
}

// Commit 提交事务
func (t *Transaction) Commit() error {
	return nil
}

// Rollback 回滚事务
func (t *Transaction) Rollback() error {
	return nil
}

// GetOptions 获取事务选项
func (t *Transaction) GetOptions() TransactionOptions {
	return t.options
}

// TransactionOptions 事务选项
type TransactionOptions struct {
	IsolationLevel string
	ReadOnly       bool
	Timeout        int
	RetryCount     int
}
