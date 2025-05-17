package concord

import (
	"testing"
	"time"

	"github.com/concordkv/client/go/concord"
)

// 测试事务超时功能
func TestTransactionTimeout(t *testing.T) {
	c, cleanup := setupTransactionTest(t)
	defer cleanup()

	// 创建一个超时时间非常短的事务
	tx := c.NewTransaction().SetTimeout(1) // 1秒超时

	// 等待超过超时时间
	time.Sleep(2 * time.Second)

	// 尝试操作已超时事务
	_, err := tx.Get("account1")
	if err == nil {
		t.Error("预期事务已超时，但Get操作没有返回错误")
	}

	// 尝试提交已超时事务
	err = tx.Commit()
	if err == nil {
		t.Error("预期事务已超时，但Commit操作没有返回错误")
	}
}

// 测试事务超时时间设置
func TestTransactionTimeoutSetting(t *testing.T) {
	c, cleanup := setupTransactionTest(t)
	defer cleanup()

	// 创建事务并检查默认超时时间
	tx1 := c.NewTransaction()
	options1 := tx1.GetOptions()
	if options1.Timeout != 30 {
		t.Errorf("默认超时时间应为30秒，但得到%d秒", options1.Timeout)
	}

	// 创建自定义超时时间的事务
	tx2 := c.NewTransaction().SetTimeout(10)
	options2 := tx2.GetOptions()
	if options2.Timeout != 10 {
		t.Errorf("自定义超时时间应为10秒，但得到%d秒", options2.Timeout)
	}

	// 测试链式调用
	tx3 := c.NewTransaction().SetTimeout(5).WithIsolation(concord.IsolationSerializable).ReadOnly()
	options3 := tx3.GetOptions()
	if options3.Timeout != 5 {
		t.Errorf("链式调用后超时时间应为5秒，但得到%d秒", options3.Timeout)
	}
	if options3.IsolationLevel != concord.IsolationSerializable {
		t.Errorf("隔离级别应为串行化，但得到%s", options3.IsolationLevel)
	}
	if !options3.ReadOnly {
		t.Error("事务应该是只读的")
	}
}
