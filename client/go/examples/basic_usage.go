package main

import (
	"fmt"
	"log"
	"time"

	"github.com/concordkv/client/concord"
)

func main() {
	// 创建客户端配置
	config := concord.Config{
		Endpoints:   []string{"127.0.0.1:5001", "127.0.0.1:5002", "127.0.0.1:5003"},
		Timeout:     3 * time.Second,
		RetryCount:  3,
		EnableCache: true,
		CacheSize:   1000,
		CacheTTL:    5 * time.Minute,
	}

	// 创建客户端
	client, err := concord.NewClient(config)
	if err != nil {
		log.Fatalf("创建客户端失败: %v", err)
	}
	defer client.Close()

	// 基本操作
	fmt.Println("执行基本操作...")

	// 设置键值
	err = client.Set("greeting", "你好，ConcordKV!")
	if err != nil {
		log.Fatalf("设置键值失败: %v", err)
	}
	fmt.Println("已设置键 'greeting'")

	// 获取键值
	value, err := client.Get("greeting")
	if err != nil {
		log.Printf("获取键值失败: %v", err)
	} else {
		fmt.Printf("键 'greeting' 的值: %s\n", value)
	}

	// 删除键值
	err = client.Delete("greeting")
	if err != nil {
		log.Printf("删除键值失败: %v", err)
	} else {
		fmt.Println("已删除键 'greeting'")
	}

	// 事务操作
	fmt.Println("\n执行事务操作...")
	txn := client.NewTransaction()

	// 在事务中设置多个键值
	err = txn.Set("counter", "1")
	if err != nil {
		log.Fatalf("事务设置失败: %v", err)
	}

	err = txn.Set("message", "事务完成")
	if err != nil {
		log.Fatalf("事务设置失败: %v", err)
	}

	// 提交事务
	err = txn.Commit()
	if err != nil {
		log.Fatalf("事务提交失败: %v", err)
	}
	fmt.Println("事务提交成功")

	// 验证事务结果
	value, err = client.Get("counter")
	if err != nil {
		log.Printf("获取键值失败: %v", err)
	} else {
		fmt.Printf("键 'counter' 的值: %s\n", value)
	}

	value, err = client.Get("message")
	if err != nil {
		log.Printf("获取键值失败: %v", err)
	} else {
		fmt.Printf("键 'message' 的值: %s\n", value)
	}

	fmt.Println("\n客户端示例完成")
}
