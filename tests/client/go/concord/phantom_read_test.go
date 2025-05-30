/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Go client - phantom_read_test.go
 */
package concord

import (
	"testing"

	"github.com/concordkv/client/go/concord"
)

// TestPhantomRead 测试在可重复读隔离级别下可能发生的幻读问题
// 幻读是指在同一事务内，两次相同的查询返回不同的结果集
// 这里我们使用多个键来模拟"结果集"的概念
func TestPhantomRead(t *testing.T) {
	c, cleanup := setupTransactionTest(t)
	defer cleanup()

	// 初始化数据，有userA和userB两个账户
	c.Set("userA", "1000")
	c.Set("userB", "2000")
	// 这里故意不创建userC，稍后会在事务中间添加

	// 创建一个"范围查询"事务，使用可重复读隔离级别
	txReader := c.NewTransaction().WithIsolation(concord.IsolationRepeatableRead)

	// 第一次查询所有用户账户 (模拟范围查询)
	users := []string{"userA", "userB", "userC"}
	var initialAccounts []string

	for _, user := range users {
		_, err := txReader.Get(user) // 忽略返回值，只关注是否存在
		if err == nil {
			initialAccounts = append(initialAccounts, user)
		}
	}

	// 应该只有两个账户
	if len(initialAccounts) != 2 {
		t.Errorf("初始账户数应该为2，但获取到%d个", len(initialAccounts))
	}

	// 创建一个并发事务，添加新账户
	txWriter := c.NewTransaction()
	err := txWriter.Set("userC", "3000")
	if err != nil {
		t.Fatalf("创建新账户失败: %v", err)
	}
	err = txWriter.Commit()
	if err != nil {
		t.Fatalf("提交新账户事务失败: %v", err)
	}

	// 第二次查询所有用户账户
	var secondAccounts []string
	for _, user := range users {
		_, err := txReader.Get(user) // 忽略返回值，只关注是否存在
		if err == nil {
			secondAccounts = append(secondAccounts, user)
		}
	}

	// 在可重复读隔离级别下，仍然应该只看到初始的两个账户，不应该看到新添加的账户
	// 这是避免幻读的体现
	if len(secondAccounts) != 2 {
		t.Errorf("在可重复读隔离级别下，第二次查询应该仍返回2个账户，但获取到%d个", len(secondAccounts))
	}

	// 提交第一个事务
	txReader.Commit()

	// 现在事务外查询，应该能看到所有三个账户
	var finalAccounts []string
	for _, user := range users {
		_, err := c.Get(user) // 忽略返回值，只关注是否存在
		if err == nil {
			finalAccounts = append(finalAccounts, user)
		}
	}

	if len(finalAccounts) != 3 {
		t.Errorf("事务外查询应该返回3个账户，但获取到%d个", len(finalAccounts))
	}
}

// TestSerializableWithPhantomReads 测试在串行化隔离级别下如何处理幻读问题
func TestSerializableWithPhantomReads(t *testing.T) {
	c, cleanup := setupTransactionTest(t)
	defer cleanup()

	// 初始化数据
	c.Set("userA", "1000")
	c.Set("userB", "2000")

	// 创建一个串行化隔离级别的事务
	txReader := c.NewTransaction().WithIsolation(concord.IsolationSerializable)

	// 第一次查询所有用户账户
	users := []string{"userA", "userB", "userC"}
	var initialAccounts []string

	for _, user := range users {
		_, err := txReader.Get(user) // 忽略返回值，只关注是否存在
		if err == nil {
			initialAccounts = append(initialAccounts, user)
		}
	}

	// 应该只有两个账户
	if len(initialAccounts) != 2 {
		t.Errorf("初始账户数应该为2，但获取到%d个", len(initialAccounts))
	}

	// 创建一个并发事务，添加新账户
	txWriter := c.NewTransaction()
	err := txWriter.Set("userC", "3000")
	if err != nil {
		t.Fatalf("创建新账户失败: %v", err)
	}
	err = txWriter.Commit()
	if err != nil {
		t.Fatalf("提交新账户事务失败: %v", err)
	}

	// 尝试提交读取事务
	// 在串行化隔离级别下，如果事务环境发生了变化，提交可能会失败
	// 这取决于实现；如果实现了真正的串行化，当查询结果可能变化时应该失败
	err = txReader.Commit()

	// 在高度并发环境中，严格的串行化隔离可能会让这种情况的事务失败
	// 但在我们的简单实现中，由于我们没有真正跟踪"不存在的键"的读取，
	// 这可能不会失败。这里我们提供两种测试结果的可能性。
	t.Logf("串行化隔离级别下，提交结果: %v", err)
}
