package concord

import (
	"fmt"
	"testing"

	mockpkg "github.com/concordkv/tests/client/go/mock"
)

// TestBatchGet 测试批量获取功能
func TestBatchGet(t *testing.T) {
	// 创建mock客户端
	client, cleanup := setupBatchTest(t)
	defer cleanup()

	// 创建测试数据
	keys := []string{"batch:1", "batch:2", "batch:3", "batch:nonexistent"}

	// 执行批量获取
	result, err := client.BatchGet(keys)
	if err != nil {
		t.Fatalf("批量获取失败: %v", err)
	}

	// 验证结果
	if len(result.Values) != 3 {
		t.Errorf("应该成功获取3个值，但实际获取到%d个", len(result.Values))
	}

	// 验证成功标记
	for _, key := range keys[:3] {
		if !result.Succeeded[key] {
			t.Errorf("键 %s 应该成功获取，但标记为失败", key)
		}
	}

	// 验证不存在的键
	if result.Succeeded["batch:nonexistent"] {
		t.Error("不存在的键不应该成功获取")
	}

	if result.Errors["batch:nonexistent"] == nil {
		t.Error("不存在的键应该有错误信息")
	}
}

// TestBatchSet 测试批量设置功能
func TestBatchSet(t *testing.T) {
	// 创建mock客户端
	client, cleanup := setupBatchTest(t)
	defer cleanup()

	// 创建测试数据
	pairs := []mockpkg.KeyValue{
		{Key: "batch:set:1", Value: "value1"},
		{Key: "batch:set:2", Value: "value2"},
		{Key: "batch:set:3", Value: "value3"},
	}

	// 执行批量设置
	result, err := client.BatchSet(pairs)
	if err != nil {
		t.Fatalf("批量设置失败: %v", err)
	}

	// 验证所有键都设置成功
	for _, pair := range pairs {
		if !result.Succeeded[pair.Key] {
			t.Errorf("键 %s 应该成功设置，但标记为失败", pair.Key)
		}
	}

	// 验证值已正确设置
	for _, pair := range pairs {
		value, err := client.Get(pair.Key)
		if err != nil {
			t.Errorf("获取键 %s 失败: %v", pair.Key, err)
		}
		if value != pair.Value {
			t.Errorf("键 %s 的值应为 %s，但获取到 %s", pair.Key, pair.Value, value)
		}
	}
}

// TestBatchDelete 测试批量删除功能
func TestBatchDelete(t *testing.T) {
	// 创建mock客户端
	client, cleanup := setupBatchTest(t)
	defer cleanup()

	// 创建测试数据
	keys := []string{"batch:delete:1", "batch:delete:2", "batch:delete:3"}
	for i, key := range keys {
		value := fmt.Sprintf("value%d", i+1)
		client.Set(key, value)
	}

	// 执行批量删除
	result, err := client.BatchDelete(keys)
	if err != nil {
		t.Fatalf("批量删除失败: %v", err)
	}

	// 验证所有键都删除成功
	for _, key := range keys {
		if !result.Succeeded[key] {
			t.Errorf("键 %s 应该成功删除，但标记为失败", key)
		}

		// 验证键已被删除
		_, err := client.Get(key)
		if err == nil {
			t.Errorf("键 %s 应该已被删除，但仍能获取", key)
		}
	}
}

// TestTransactionBatchOperations 测试事务中的批量操作
func TestTransactionBatchOperations(t *testing.T) {
	// 创建mock客户端
	client, cleanup := setupBatchTest(t)
	defer cleanup()

	// 创建事务
	tx := client.NewTransaction()

	// 在事务中进行批量设置
	pairs := []mockpkg.KeyValue{
		{Key: "tx:batch:1", Value: "txvalue1"},
		{Key: "tx:batch:2", Value: "txvalue2"},
	}

	result, err := tx.BatchSet(pairs)
	if err != nil {
		t.Fatalf("事务中的批量设置失败: %v", err)
	}

	// 验证在事务中所有键都设置成功
	for _, pair := range pairs {
		if !result.Succeeded[pair.Key] {
			t.Errorf("事务中键 %s 应该成功设置，但标记为失败", pair.Key)
		}
	}

	// 在事务中进行批量获取
	getResult, err := tx.BatchGet([]string{"tx:batch:1", "tx:batch:2"})
	if err != nil {
		t.Fatalf("事务中的批量获取失败: %v", err)
	}

	// 验证在事务中能读取到设置的值
	for _, pair := range pairs {
		if getResult.Values[pair.Key] != pair.Value {
			t.Errorf("事务中键 %s 的值应为 %s，但获取到 %s",
				pair.Key, pair.Value, getResult.Values[pair.Key])
		}
	}

	// 提交事务
	err = tx.Commit()
	if err != nil {
		t.Fatalf("事务提交失败: %v", err)
	}

	// 验证事务外能读取到提交的值
	for _, pair := range pairs {
		value, err := client.Get(pair.Key)
		if err != nil {
			t.Errorf("获取键 %s 失败: %v", pair.Key, err)
		}
		if value != pair.Value {
			t.Errorf("键 %s 的值应为 %s，但获取到 %s", pair.Key, pair.Value, value)
		}
	}
}

// setupBatchTest 设置批量操作测试环境
func setupBatchTest(t *testing.T) (*mockpkg.Client, func()) {
	// 创建模拟客户端
	config := mockpkg.Config{
		Endpoints:   []string{"127.0.0.1:5001"},
		Timeout:     3,
		RetryCount:  3,
		EnableCache: false,
	}

	client, err := mockpkg.NewClient(config)
	if err != nil {
		t.Fatalf("创建客户端失败: %v", err)
	}

	// 初始化测试数据
	err = client.Set("batch:1", "value1")
	if err != nil {
		t.Fatalf("设置初始数据失败: %v", err)
	}
	err = client.Set("batch:2", "value2")
	if err != nil {
		t.Fatalf("设置初始数据失败: %v", err)
	}
	err = client.Set("batch:3", "value3")
	if err != nil {
		t.Fatalf("设置初始数据失败: %v", err)
	}

	// 返回清理函数
	cleanup := func() {
		client.Delete("batch:1")
		client.Delete("batch:2")
		client.Delete("batch:3")
		client.Close()
	}

	return client, cleanup
}
