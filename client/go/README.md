# ConcordKV Go客户端

这是ConcordKV的Go语言客户端实现，提供与ConcordKV分布式KV存储系统交互的接口。

## 功能特性

- 基本的键值操作（获取、设置、删除）
- 事务支持
- 客户端缓存
- 自动重试和负载均衡
- 多节点连接支持
- 监控和健康检查

## 目录结构

```
client/go/
├── concord/           # 核心客户端包
│   ├── client.go      # 基本客户端实现
│   ├── transaction.go # 事务支持
│   ├── cache.go       # 客户端缓存
│   ├── connection.go  # 网络通信
│   └── monitoring.go  # 监控与健康检查
├── examples/          # 使用示例
│   ├── basic_usage.go       # 基本使用示例
│   └── monitoring_example.go# 监控功能示例
└── README.md          # 本文件
```

## 快速开始

### 安装

```bash
go get github.com/concordkv/client
```

### 基本使用

```go
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
		Endpoints:    []string{"127.0.0.1:5001", "127.0.0.1:5002", "127.0.0.1:5003"},
		Timeout:      3 * time.Second,
		RetryCount:   3,
		EnableCache:  true,
		CacheSize:    1000,
		CacheTTL:     5 * time.Minute,
	}

	// 创建客户端
	client, err := concord.NewClient(config)
	if err != nil {
		log.Fatalf("创建客户端失败: %v", err)
	}
	defer client.Close()

	// 设置键值
	err = client.Set("greeting", "你好，ConcordKV!")
	if err != nil {
		log.Fatalf("设置键值失败: %v", err)
	}

	// 获取键值
	value, err := client.Get("greeting")
	if err != nil {
		log.Printf("获取键值失败: %v", err)
	} else {
		fmt.Printf("键 'greeting' 的值: %s\n", value)
	}
}
```

### 事务使用

```go
// 创建事务
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
```

### 监控和健康检查

```go
// 启用监控（每30秒检查一次）
if err := client.EnableMonitoring(30 * time.Second); err != nil {
    log.Printf("启用监控失败: %v", err)
}

// 获取集群健康状态
stats, err := client.GetClusterHealth()
if err != nil {
    log.Printf("获取集群健康状态失败: %v", err)
} else {
    fmt.Printf("集群总节点数: %d\n", stats.TotalNodes)
    fmt.Printf("健康节点数: %d\n", stats.HealthyNodes)
    fmt.Printf("平均延迟: %dms\n", stats.AvgLatency)
}
```

## 待完成功能

- 实现节点发现和负载均衡
- 添加更多事务隔离级别支持
- 增加批量操作支持