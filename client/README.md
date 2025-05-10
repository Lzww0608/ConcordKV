# ConcordKV 客户端库

本目录包含ConcordKV分布式键值存储系统的客户端实现。客户端库提供简单、高效的API，用于与ConcordKV集群进行交互。

## 支持的编程语言

计划支持以下编程语言的客户端库：

- Go (主要实现)
- C/C++
- Python (计划中)
- Java (计划中)

## 功能特性

- 连接管理和自动重连
- 请求负载均衡
- 智能路由（将请求直接发送到最适合的节点）
- 客户端缓存
- 批量操作
- 异步操作
- 事务支持

## 目录结构

```
client/
├── go/               - Go客户端实现
├── cpp/              - C/C++客户端实现
├── examples/         - 使用示例
└── docs/             - 客户端API文档
```

## Go客户端使用示例

```go
package main

import (
    "fmt"
    "log"
    
    "github.com/concord/client/go"
)

func main() {
    // 创建客户端
    config := concord.Config{
        Endpoints: []string{"127.0.0.1:5001", "127.0.0.1:5002", "127.0.0.1:5003"},
        Timeout:   3 * time.Second,
    }
    
    client, err := concord.NewClient(config)
    if err != nil {
        log.Fatalf("Failed to create client: %v", err)
    }
    defer client.Close()
    
    // 设置键值
    err = client.Set("greeting", "Hello, ConcordKV!")
    if err != nil {
        log.Fatalf("Failed to set key: %v", err)
    }
    
    // 获取键值
    value, err := client.Get("greeting")
    if err != nil {
        log.Fatalf("Failed to get key: %v", err)
    }
    
    fmt.Printf("Value: %s\n", value)
    
    // 使用事务
    txn := client.NewTransaction()
    txn.Set("counter", "1")
    txn.Set("message", "Transaction complete")
    
    err = txn.Commit()
    if err != nil {
        log.Fatalf("Transaction failed: %v", err)
    }
}
```

## C/C++客户端使用示例

```c
#include "concord_client.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // 创建客户端配置
    concord_config_t config;
    concord_config_init(&config);
    concord_config_add_endpoint(&config, "127.0.0.1", 5001);
    concord_config_add_endpoint(&config, "127.0.0.1", 5002);
    concord_config_add_endpoint(&config, "127.0.0.1", 5003);
    
    // 创建客户端
    concord_client_t *client = concord_client_create(&config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    // 设置键值
    int ret = concord_set(client, "greeting", "Hello, ConcordKV!");
    if (ret != 0) {
        fprintf(stderr, "Failed to set key: %d\n", ret);
        concord_client_destroy(client);
        return 1;
    }
    
    // 获取键值
    char *value = NULL;
    ret = concord_get(client, "greeting", &value);
    if (ret != 0) {
        fprintf(stderr, "Failed to get key: %d\n", ret);
        concord_client_destroy(client);
        return 1;
    }
    
    printf("Value: %s\n", value);
    
    // 释放资源
    concord_free(value);
    concord_client_destroy(client);
    
    return 0;
}
```

## 开发状态

目前客户端库处于规划阶段，正在设计API和实现基本功能。

## 贡献

欢迎贡献代码、报告问题或提出新功能建议。详情请参阅 [贡献指南](../docs/CONTRIBUTING.md)。 