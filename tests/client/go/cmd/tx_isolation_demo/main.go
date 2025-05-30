/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Go client - main.go
 */
package main

import (
	"fmt"
	"log"
	"time"

	// 修复导入路径
	mockpkg "github.com/concordkv/tests/client/go/mock"
)

// 主函数：启动事务隔离级别演示
func main() {
	fmt.Println("=== ConcordKV 事务隔离级别演示 ===")
	RunTransactionIsolationDemo()
	fmt.Println("=== 演示结束 ===")
}

// RunTransactionIsolationDemo 执行事务隔离级别演示
func RunTransactionIsolationDemo() {
	fmt.Println("开始事务隔离级别演示...")

	// 创建客户端配置
	config := mockpkg.Config{
		Endpoints:   []string{"127.0.0.1:5001", "127.0.0.1:5002", "127.0.0.1:5003"},
		Timeout:     3 * time.Second,
		RetryCount:  3,
		EnableCache: true,
		CacheSize:   1000,
		CacheTTL:    5 * time.Minute,
	}

	// 创建客户端
	client, err := mockpkg.NewClient(config)
	if err != nil {
		log.Fatalf("创建客户端失败: %v", err)
	}
	defer client.Close()

	// 初始化测试数据
	err = client.Set("account1", "1000")
	if err != nil {
		log.Fatalf("设置初始数据失败: %v", err)
	}
	err = client.Set("account2", "2000")
	if err != nil {
		log.Fatalf("设置初始数据失败: %v", err)
	}

	fmt.Println("初始账户余额:")
	printAccounts(client)

	// 演示不同的隔离级别
	demoReadUncommitted(client)
	demoReadCommitted(client)
	demoRepeatableRead(client)
	demoSerializable(client)

	fmt.Println("事务隔离级别演示完成.")
}

// 打印账户余额
func printAccounts(client *mockpkg.Client) {
	account1, err := client.Get("account1")
	if err != nil {
		fmt.Printf("读取account1失败: %v\n", err)
	}
	account2, err := client.Get("account2")
	if err != nil {
		fmt.Printf("读取account2失败: %v\n", err)
	}
	fmt.Printf("账户1余额: %s\n", account1)
	fmt.Printf("账户2余额: %s\n", account2)
	fmt.Println("------------------------")
}

// 演示读未提交隔离级别
func demoReadUncommitted(client *mockpkg.Client) {
	fmt.Println("\n=== 演示读未提交隔离级别 ===")
	fmt.Println("这种隔离级别允许读取其他事务尚未提交的数据（脏读）")

	// 重置测试数据
	client.Set("account1", "1000")
	client.Set("account2", "2000")

	// 创建第一个事务（写入者）
	txWriter := client.NewTransaction().WithIsolation(mockpkg.IsolationReadUncommitted)

	// 在事务中修改数据但不提交
	err := txWriter.Set("account1", "900")
	if err != nil {
		fmt.Printf("修改数据失败: %v\n", err)
	}

	fmt.Println("事务1: 已修改account1为900但尚未提交")

	// 创建第二个事务（读取者）
	txReader := client.NewTransaction().WithIsolation(mockpkg.IsolationReadUncommitted)

	// 读取尚未提交的数据
	val, err := txReader.Get("account1")
	if err != nil {
		fmt.Printf("读取数据失败: %v\n", err)
	} else {
		fmt.Printf("事务2: 读取account1的值: %s (读取到了未提交的数据)\n", val)
	}

	// 回滚第一个事务
	txWriter.Rollback()
	fmt.Println("事务1: 已回滚")

	// 提交第二个事务
	txReader.Commit()
	fmt.Println("事务2: 已提交")

	fmt.Println("读未提交可能导致脏读，即读取到其他事务尚未提交的数据")
	fmt.Println("------------------------")
}

// 演示读已提交隔离级别
func demoReadCommitted(client *mockpkg.Client) {
	fmt.Println("\n=== 演示读已提交隔离级别 ===")
	fmt.Println("这种隔离级别防止脏读，但可能出现不可重复读问题")

	// 重置测试数据
	client.Set("account1", "1000")
	client.Set("account2", "2000")

	// 创建第一个事务（读取者）
	txReader := client.NewTransaction().WithIsolation(mockpkg.IsolationReadCommitted)

	// 第一次读取
	val1, err := txReader.Get("account1")
	if err != nil {
		fmt.Printf("第一次读取失败: %v\n", err)
	} else {
		fmt.Printf("事务1: 第一次读取account1的值: %s\n", val1)
	}

	// 创建第二个事务（写入者）并修改数据
	txWriter := client.NewTransaction().WithIsolation(mockpkg.IsolationReadCommitted)
	err = txWriter.Set("account1", "800")
	if err != nil {
		fmt.Printf("修改数据失败: %v\n", err)
	}

	// 提交第二个事务
	err = txWriter.Commit()
	if err != nil {
		fmt.Printf("提交事务失败: %v\n", err)
	}
	fmt.Println("事务2: 已修改account1为800并提交")

	// 在第一个事务中再次读取
	val2, err := txReader.Get("account1")
	if err != nil {
		fmt.Printf("第二次读取失败: %v\n", err)
	} else {
		fmt.Printf("事务1: 第二次读取account1的值: %s (与第一次读取的值不同)\n", val2)
	}

	// 提交第一个事务
	txReader.Commit()
	fmt.Println("事务1: 已提交")

	fmt.Println("读已提交会出现不可重复读问题，即同一事务中多次读取同一数据可能得到不同结果")
	fmt.Println("------------------------")
}

// 演示可重复读隔离级别
func demoRepeatableRead(client *mockpkg.Client) {
	fmt.Println("\n=== 演示可重复读隔离级别 ===")
	fmt.Println("这种隔离级别防止脏读和不可重复读，但可能出现幻读问题")

	// 重置测试数据
	client.Set("account1", "1000")
	client.Set("account2", "2000")

	// 创建第一个事务（读取者）
	txReader := client.NewTransaction().WithIsolation(mockpkg.IsolationRepeatableRead)

	// 第一次读取
	val1, err := txReader.Get("account1")
	if err != nil {
		fmt.Printf("第一次读取失败: %v\n", err)
	} else {
		fmt.Printf("事务1: 第一次读取account1的值: %s\n", val1)
	}

	// 创建第二个事务（写入者）并修改数据
	txWriter := client.NewTransaction().WithIsolation(mockpkg.IsolationRepeatableRead)
	err = txWriter.Set("account1", "700")
	if err != nil {
		fmt.Printf("修改数据失败: %v\n", err)
	}

	// 提交第二个事务
	err = txWriter.Commit()
	if err != nil {
		fmt.Printf("提交事务失败: %v\n", err)
	}
	fmt.Println("事务2: 已修改account1为700并提交")

	// 在第一个事务中再次读取
	val2, err := txReader.Get("account1")
	if err != nil {
		fmt.Printf("第二次读取失败: %v\n", err)
	} else {
		fmt.Printf("事务1: 第二次读取account1的值: %s (与第一次读取的值相同，即使数据已被其他事务修改)\n", val2)
	}

	// 提交第一个事务
	txReader.Commit()
	fmt.Println("事务1: 已提交")

	// 事务外读取当前值
	val, err := client.Get("account1")
	if err != nil {
		fmt.Printf("读取当前值失败: %v\n", err)
	} else {
		fmt.Printf("当前account1的值: %s\n", val)
	}

	fmt.Println("可重复读确保同一事务中多次读取同一数据得到相同结果")
	fmt.Println("------------------------")
}

// 演示串行化隔离级别
func demoSerializable(client *mockpkg.Client) {
	fmt.Println("\n=== 演示串行化隔离级别 ===")
	fmt.Println("这种隔离级别防止所有并发问题，包括脏读、不可重复读和幻读")

	// 重置测试数据
	client.Set("account1", "1000")
	client.Set("account2", "2000")

	// 创建第一个事务（转账事务）
	txTransfer := client.NewTransaction().WithIsolation(mockpkg.IsolationSerializable)

	// 读取两个账户余额
	balance1, err := txTransfer.Get("account1")
	if err != nil {
		fmt.Printf("读取account1失败: %v\n", err)
		return
	}

	balance2, err := txTransfer.Get("account2")
	if err != nil {
		fmt.Printf("读取account2失败: %v\n", err)
		return
	}

	fmt.Printf("事务1: 读取两个账户的初始余额 - account1: %s, account2: %s\n", balance1, balance2)

	// 模拟并发事务干扰
	fmt.Println("模拟并发干扰: 另一个事务修改account1")
	interfereTx := client.NewTransaction()
	err = interfereTx.Set("account1", "1200")
	if err != nil {
		fmt.Printf("干扰事务修改失败: %v\n", err)
	}
	err = interfereTx.Commit()
	if err != nil {
		fmt.Printf("干扰事务提交失败: %v\n", err)
	} else {
		fmt.Println("干扰事务: 已修改account1为1200并提交")
	}

	// 在第一个事务中转账
	fmt.Println("事务1: 尝试从account1转账300到account2")
	// 由于使用串行化隔离级别，如果其他事务修改了我们读取的数据，提交时会失败

	// 设置新值
	err = txTransfer.Set("account1", "700") // 1000 - 300
	if err != nil {
		fmt.Printf("修改account1失败: %v\n", err)
		return
	}

	err = txTransfer.Set("account2", "2300") // 2000 + 300
	if err != nil {
		fmt.Printf("修改account2失败: %v\n", err)
		return
	}

	// 尝试提交事务
	err = txTransfer.Commit()
	if err != nil {
		fmt.Printf("事务1提交失败: %v (预期失败，因为account1已被其他事务修改)\n", err)
	} else {
		fmt.Println("事务1: 转账成功并提交")
	}

	// 查看当前账户余额
	fmt.Println("当前账户余额:")
	printAccounts(client)

	fmt.Println("串行化隔离级别会检测并发冲突，如果检测到冲突会导致事务失败，保证数据一致性")
	fmt.Println("这是最高的隔离级别，但会降低并发性能")
	fmt.Println("------------------------")
}
