/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Go client batch operations example
 */

package main

import (
	"fmt"
	"log"
	"time"

	"github.com/concordkv/client/go/concord"
)

func main() {
	// 创建客户端配置
	config := concord.Config{
		Endpoints:   []string{"127.0.0.1:5001", "127.0.0.1:5002", "127.0.0.1:5003"},
		Timeout:     3 * time.Second,
		RetryCount:  3,
		EnableCache: true,
	}

	// 创建客户端
	client, err := concord.NewClient(config)
	if err != nil {
		log.Fatalf("创建客户端失败: %v", err)
	}
	defer client.Close()

	// 演示批量操作
	fmt.Println("=== ConcordKV 批量操作示例 ===")

	// 1. 批量设置示例
	batchSetExample(client)

	// 2. 批量获取示例
	batchGetExample(client)

	// 3. 批量删除示例
	batchDeleteExample(client)

	// 4. 事务内批量操作示例
	transactionBatchExample(client)

	fmt.Println("=== 批量操作示例结束 ===")
}

// 批量设置示例
func batchSetExample(client *concord.Client) {
	fmt.Println("\n--- 批量设置示例 ---")

	// 准备批量设置的键值对
	pairs := []concord.KeyValue{
		{Key: "batch:user:1", Value: "张三"},
		{Key: "batch:user:2", Value: "李四"},
		{Key: "batch:user:3", Value: "王五"},
		{Key: "batch:user:4", Value: "赵六"},
		{Key: "batch:user:5", Value: "钱七"},
	}

	// 批量设置
	result, err := client.BatchSet(pairs)
	if err != nil {
		log.Printf("批量设置失败: %v", err)
		return
	}

	// 检查结果
	fmt.Printf("批量设置完成，成功: %d, 失败: %d\n",
		countSuccesses(result.Succeeded),
		len(pairs)-countSuccesses(result.Succeeded))

	// 打印失败的键（如果有）
	for key, succeeded := range result.Succeeded {
		if !succeeded {
			fmt.Printf("  键 '%s' 设置失败: %v\n", key, result.Errors[key])
		}
	}
}

// 批量获取示例
func batchGetExample(client *concord.Client) {
	fmt.Println("\n--- 批量获取示例 ---")

	// 准备要获取的键
	keys := []string{
		"batch:user:1",
		"batch:user:2",
		"batch:user:3",
		"batch:user:4",
		"batch:user:5",
		"batch:user:999", // 不存在的键
	}

	// 批量获取
	result, err := client.BatchGet(keys)
	if err != nil {
		log.Printf("批量获取失败: %v", err)
		return
	}

	// 检查结果
	fmt.Printf("批量获取完成，成功: %d, 失败: %d\n",
		countSuccesses(result.Succeeded),
		len(keys)-countSuccesses(result.Succeeded))

	// 打印获取到的值
	fmt.Println("获取到的值:")
	for key, succeeded := range result.Succeeded {
		if succeeded {
			fmt.Printf("  %s = %s\n", key, result.Values[key])
		} else {
			fmt.Printf("  %s = <获取失败: %v>\n", key, result.Errors[key])
		}
	}
}

// 批量删除示例
func batchDeleteExample(client *concord.Client) {
	fmt.Println("\n--- 批量删除示例 ---")

	// 准备要删除的键
	keys := []string{
		"batch:user:3",
		"batch:user:4",
		"batch:user:5",
	}

	// 批量删除
	result, err := client.BatchDelete(keys)
	if err != nil {
		log.Printf("批量删除失败: %v", err)
		return
	}

	// 检查结果
	fmt.Printf("批量删除完成，成功: %d, 失败: %d\n",
		countSuccesses(result.Succeeded),
		len(keys)-countSuccesses(result.Succeeded))

	// 打印失败的键（如果有）
	for key, succeeded := range result.Succeeded {
		if !succeeded {
			fmt.Printf("  键 '%s' 删除失败: %v\n", key, result.Errors[key])
		}
	}

	// 验证删除结果
	keysCheck := []string{"batch:user:1", "batch:user:2", "batch:user:3", "batch:user:4", "batch:user:5"}
	fmt.Println("验证删除结果:")

	checkResult, _ := client.BatchGet(keysCheck)
	for key, succeeded := range checkResult.Succeeded {
		if succeeded {
			fmt.Printf("  %s 仍然存在，值为: %s\n", key, checkResult.Values[key])
		} else {
			fmt.Printf("  %s 已被删除\n", key)
		}
	}
}

// 事务内批量操作示例
func transactionBatchExample(client *concord.Client) {
	fmt.Println("\n--- 事务内批量操作示例 ---")

	// 创建事务
	tx := client.NewTransaction().WithIsolation(concord.IsolationSerializable)

	// 在事务中批量设置
	pairs := []concord.KeyValue{
		{Key: "tx:batch:item:1", Value: "商品1"},
		{Key: "tx:batch:item:2", Value: "商品2"},
		{Key: "tx:batch:item:3", Value: "商品3"},
	}

	setResult, err := tx.BatchSet(pairs)
	if err != nil {
		log.Printf("事务批量设置失败: %v", err)
		tx.Rollback()
		return
	}

	fmt.Printf("事务批量设置完成，成功: %d\n", countSuccesses(setResult.Succeeded))

	// 在事务中批量获取
	getResult, err := tx.BatchGet([]string{"tx:batch:item:1", "tx:batch:item:2", "tx:batch:item:3"})
	if err != nil {
		log.Printf("事务批量获取失败: %v", err)
		tx.Rollback()
		return
	}

	fmt.Println("事务中读取的值:")
	for key, val := range getResult.Values {
		fmt.Printf("  %s = %s\n", key, val)
	}

	// 提交事务
	err = tx.Commit()
	if err != nil {
		log.Printf("事务提交失败: %v", err)
		return
	}

	fmt.Println("事务已提交")

	// 验证事务外的结果
	fmt.Println("事务外验证:")
	keys := []string{"tx:batch:item:1", "tx:batch:item:2", "tx:batch:item:3"}
	result, _ := client.BatchGet(keys)

	for key, succeeded := range result.Succeeded {
		if succeeded {
			fmt.Printf("  %s = %s\n", key, result.Values[key])
		} else {
			fmt.Printf("  %s = <获取失败: %v>\n", key, result.Errors[key])
		}
	}
}

// 计算成功操作的数量
func countSuccesses(succeeded map[string]bool) int {
	count := 0
	for _, s := range succeeded {
		if s {
			count++
		}
	}
	return count
}
