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
│   ├── basic_usage.go      # 基本使用示例
│   └── monitoring_example.go# 监控功能示例
├── cmd/               # 命令行工具
│   └── tx_isolation_demo/  # 事务隔离级别演示
│       └── main.go         # 演示程序入口
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

### 事务隔离级别

```go
// 创建一个使用可重复读隔离级别的事务
txn := client.NewTransaction().WithIsolation(concord.IsolationRepeatableRead)

// 创建一个使用串行化隔离级别的事务
txn := client.NewTransaction().WithIsolation(concord.IsolationSerializable)

// 创建一个使用读已提交隔离级别的事务
txn := client.NewTransaction().WithIsolation(concord.IsolationReadCommitted)

// 创建一个使用读未提交隔离级别的事务
txn := client.NewTransaction().WithIsolation(concord.IsolationReadUncommitted)

// 创建一个只读事务
txn := client.NewTransaction().ReadOnly()

// 设置事务超时（30秒）
txn := client.NewTransaction().SetTimeout(30)
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

## 节点发现和负载均衡

ConcordKV Go客户端现在支持节点发现和负载均衡功能，能够自动发现集群中的新节点并智能地分配请求负载。

### 特性

- 多种节点发现方式：
  - 静态配置
  - DNS SRV记录
  - 服务注册中心
- 多种负载均衡策略：
  - 随机选择
  - 轮询（Round Robin）
  - 加权负载均衡（根据节点性能）
  - 最少连接
  - 一致性哈希（特定键总是路由到相同节点）
- 自动故障转移和恢复
- 自动维护连接池

### 使用示例

```go
// 创建客户端
client, _ := concord.NewClient(config)

// 启用监控（为负载均衡提供性能数据）
client.EnableMonitoring(30 * time.Second)

// 配置节点发现
discoveryConfig := concord.DiscoveryConfig{
    Type:            concord.DiscoveryTypeService,
    ServiceRegistryURL: "http://registry.example.com",
    ServiceName:     "concordkv",
    RefreshInterval: 60 * time.Second,
    BalanceStrategy: concord.BalanceWeighted,
    AutoDiscover:    true,
}

// 启用节点发现和负载均衡
client.EnableDiscovery(discoveryConfig)
```

完整示例请参见 `examples/discovery_example.go` 和 `cmd/tx_isolation_demo/main.go`。

## 待完成功能

- 添加更多事务隔离级别支持 ✓ 已完成
- 增加批量操作支持 ✓ 已完成

## 批量操作

ConcordKV Go客户端支持批量操作，可以一次性处理多个键或键值对，提高性能并简化代码。

### 批量获取

```go
// 批量获取多个键的值
keys := []string{"user:1", "user:2", "user:3"}
result, err := client.BatchGet(keys)
if err != nil {
    log.Printf("批量获取失败: %v", err)
    return
}

// 处理结果
for key, value := range result.Values {
    fmt.Printf("%s = %s\n", key, value)
}

// 检查失败的操作
for key, succeeded := range result.Succeeded {
    if !succeeded {
        fmt.Printf("获取 %s 失败: %v\n", key, result.Errors[key])
    }
}
```

### 批量设置

```go
// 准备批量设置的键值对
pairs := []concord.KeyValue{
    {Key: "user:1", Value: "张三"},
    {Key: "user:2", Value: "李四"},
    {Key: "user:3", Value: "王五"},
}

// 批量设置
result, err := client.BatchSet(pairs)
if err != nil {
    log.Printf("批量设置失败: %v", err)
    return
}

// 检查结果
for key, succeeded := range result.Succeeded {
    if !succeeded {
        fmt.Printf("设置 %s 失败: %v\n", key, result.Errors[key])
    }
}
```

### 事务中的批量操作

```go
// 创建事务
tx := client.NewTransaction()

// 在事务中批量获取
getResult, err := tx.BatchGet([]string{"account:1", "account:2"})
if err != nil {
    tx.Rollback()
    return
}

// 在事务中批量设置
pairs := []concord.KeyValue{
    {Key: "account:1", Value: "900"},
    {Key: "account:2", Value: "2100"},
}
setResult, err := tx.BatchSet(pairs)
if err != nil {
    tx.Rollback()
    return
}

// 提交事务
tx.Commit()
```