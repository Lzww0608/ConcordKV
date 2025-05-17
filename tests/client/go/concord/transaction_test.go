package concord

import (
	"testing"
	"time"

	"github.com/concordkv/client/go/concord"
)

// 模拟数据存储
type mockStore struct {
	data map[string]string
}

// 创建模拟事务测试环境
func setupTransactionTest(t *testing.T) (*concord.Client, func()) {
	// 创建模拟客户端
	config := concord.Config{
		Endpoints:   []string{"127.0.0.1:5001"},
		Timeout:     3 * time.Second,
		RetryCount:  3,
		EnableCache: false,
	}

	client, err := concord.NewClient(config)
	if err != nil {
		t.Fatalf("创建客户端失败: %v", err)
	}

	// 初始化测试数据
	err = client.Set("account1", "1000")
	if err != nil {
		t.Fatalf("设置初始数据失败: %v", err)
	}
	err = client.Set("account2", "2000")
	if err != nil {
		t.Fatalf("设置初始数据失败: %v", err)
	}

	// 返回清理函数
	cleanup := func() {
		client.Delete("account1")
		client.Delete("account2")
		client.Close()
	}

	return client, cleanup
}

// 测试读未提交隔离级别
func TestIsolationReadUncommitted(t *testing.T) {
	c, cleanup := setupTransactionTest(t)
	defer cleanup()

	// 创建第一个事务（写入者）
	txWriter := c.NewTransaction().WithIsolation(concord.IsolationReadUncommitted)

	// 在事务中修改数据但不提交
	err := txWriter.Set("account1", "900")
	if err != nil {
		t.Fatalf("修改数据失败: %v", err)
	}

	// 创建第二个事务（读取者）
	txReader := c.NewTransaction().WithIsolation(concord.IsolationReadUncommitted)

	// 读取尚未提交的数据（读未提交级别应该能看到未提交的数据）
	val, err := txReader.Get("account1")
	if err != nil {
		t.Fatalf("读取数据失败: %v", err)
	}

	// 在读未提交隔离级别，应该能看到未提交的值
	if val != "900" {
		t.Errorf("读未提交隔离级别应该能读取到未提交的数据，期望值: 900, 实际值: %s", val)
	}

	// 回滚第一个事务
	txWriter.Rollback()

	// 提交第二个事务
	txReader.Commit()
}

// 测试读已提交隔离级别
func TestIsolationReadCommitted(t *testing.T) {
	c, cleanup := setupTransactionTest(t)
	defer cleanup()

	// 创建第一个事务（读取者）
	txReader := c.NewTransaction().WithIsolation(concord.IsolationReadCommitted)

	// 第一次读取
	val1, err := txReader.Get("account1")
	if err != nil {
		t.Fatalf("第一次读取失败: %v", err)
	}

	// 创建第二个事务（写入者）并修改数据
	txWriter := c.NewTransaction().WithIsolation(concord.IsolationReadCommitted)
	err = txWriter.Set("account1", "800")
	if err != nil {
		t.Fatalf("修改数据失败: %v", err)
	}

	// 提交第二个事务
	err = txWriter.Commit()
	if err != nil {
		t.Fatalf("提交事务失败: %v", err)
	}

	// 在第一个事务中再次读取
	val2, err := txReader.Get("account1")
	if err != nil {
		t.Fatalf("第二次读取失败: %v", err)
	}

	// 在读已提交隔离级别，第二次读取应该能看到其他事务提交的更改
	if val1 == val2 {
		t.Errorf("读已提交隔离级别应该在不同时间读取到不同值，第一次读取: %s, 第二次读取: %s", val1, val2)
	}

	if val2 != "800" {
		t.Errorf("读已提交隔离级别应该能读取到其他事务已提交的数据，期望值: 800, 实际值: %s", val2)
	}

	// 提交第一个事务
	txReader.Commit()
}

// 测试可重复读隔离级别
func TestIsolationRepeatableRead(t *testing.T) {
	c, cleanup := setupTransactionTest(t)
	defer cleanup()

	// 创建第一个事务（读取者）
	txReader := c.NewTransaction().WithIsolation(concord.IsolationRepeatableRead)

	// 第一次读取
	val1, err := txReader.Get("account1")
	if err != nil {
		t.Fatalf("第一次读取失败: %v", err)
	}

	// 创建第二个事务（写入者）并修改数据
	txWriter := c.NewTransaction().WithIsolation(concord.IsolationRepeatableRead)
	err = txWriter.Set("account1", "700")
	if err != nil {
		t.Fatalf("修改数据失败: %v", err)
	}

	// 提交第二个事务
	err = txWriter.Commit()
	if err != nil {
		t.Fatalf("提交事务失败: %v", err)
	}

	// 在第一个事务中再次读取
	val2, err := txReader.Get("account1")
	if err != nil {
		t.Fatalf("第二次读取失败: %v", err)
	}

	// 在可重复读隔离级别，多次读取同一数据应该返回相同的结果
	if val1 != val2 {
		t.Errorf("可重复读隔离级别应该在不同时间读取到相同值，第一次读取: %s, 第二次读取: %s", val1, val2)
	}

	// 提交第一个事务
	txReader.Commit()

	// 事务外读取当前值
	val, err := c.Get("account1")
	if err != nil {
		t.Fatalf("读取当前值失败: %v", err)
	}

	// 事务外应该能读取到最新值
	if val != "700" {
		t.Errorf("事务外应该能读取到最新值，期望值: 700, 实际值: %s", val)
	}
}

// 测试串行化隔离级别
func TestIsolationSerializable(t *testing.T) {
	c, cleanup := setupTransactionTest(t)
	defer cleanup()

	// 创建第一个事务（转账事务）
	txTransfer := c.NewTransaction().WithIsolation(concord.IsolationSerializable)

	// 读取两个账户余额
	_, err := txTransfer.Get("account1")
	if err != nil {
		t.Fatalf("读取account1失败: %v", err)
	}

	_, err = txTransfer.Get("account2")
	if err != nil {
		t.Fatalf("读取account2失败: %v", err)
	}

	// 模拟并发事务干扰
	interfereTx := c.NewTransaction()
	err = interfereTx.Set("account1", "1200")
	if err != nil {
		t.Fatalf("干扰事务修改失败: %v", err)
	}
	err = interfereTx.Commit()
	if err != nil {
		t.Fatalf("干扰事务提交失败: %v", err)
	}

	// 在第一个事务中转账
	err = txTransfer.Set("account1", "700") // 1000 - 300
	if err != nil {
		t.Fatalf("修改account1失败: %v", err)
	}

	err = txTransfer.Set("account2", "2300") // 2000 + 300
	if err != nil {
		t.Fatalf("修改account2失败: %v", err)
	}

	// 尝试提交事务，在串行化隔离级别下应该会失败
	err = txTransfer.Commit()

	// 在串行化隔离级别，如果其他事务修改了我们读取的数据，提交应该失败
	if err == nil {
		t.Errorf("串行化隔离级别下，当读取的数据被其他事务修改时，提交应该失败")
	}
}
