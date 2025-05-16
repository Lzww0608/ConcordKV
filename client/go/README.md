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

完整示例请参见 `examples/discovery_example.go`。

## 待完成功能

- 添加更多事务隔离级别支持
- 增加批量操作支持