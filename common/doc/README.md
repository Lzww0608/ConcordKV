# ConcordKV 通用工具和配置

本目录包含 ConcordKV 分布式键值存储系统的通用工具、配置管理和其他可共享的组件。

## 目录结构

```
common/
├── config/           - 配置管理
├── util/             - 通用工具函数
├── metrics/          - 监控指标
├── testing/          - 测试工具
└── doc/              - 内部文档
```

## 组件说明

### 配置管理

配置管理模块提供统一的配置加载、验证和访问机制，支持以下功能：

- 多种配置源（文件、环境变量、命令行参数）
- 配置项验证
- 动态配置更新
- 配置模板和默认值

### 通用工具函数

包含各种可重用的工具函数：

- 日志工具
- 加密工具
- 网络工具
- 时间和定时器
- 数据格式转换

### 监控指标

提供统一的指标收集和报告框架：

- 性能指标
- 资源使用情况
- 健康状态
- 警报阈值

### 测试工具

辅助测试的工具和框架：

- 模拟客户端
- 故障注入
- 性能基准测试
- 测试数据生成

## 使用示例

### 配置管理

```go
// Go示例
package main

import (
    "fmt"
    "github.com/concord/common/config"
)

func main() {
    // 加载配置
    cfg, err := config.Load("config.yaml")
    if err != nil {
        panic(err)
    }
    
    // 获取配置值
    port := cfg.GetInt("server.port", 5000)  // 默认值为5000
    host := cfg.GetString("server.host", "localhost")
    
    fmt.Printf("Server: %s:%d\n", host, port)
    
    // 监听配置变更
    cfg.Watch("server.port", func(oldValue, newValue interface{}) {
        fmt.Printf("Port changed from %v to %v\n", oldValue, newValue)
    })
}
```

```c
// C示例
#include "concord_config.h"
#include <stdio.h>

int main() {
    // 加载配置
    concord_config_t *cfg = concord_config_load("config.yaml");
    if (!cfg) {
        fprintf(stderr, "Failed to load config\n");
        return 1;
    }
    
    // 获取配置值
    int port = concord_config_get_int(cfg, "server.port", 5000);
    const char *host = concord_config_get_string(cfg, "server.host", "localhost");
    
    printf("Server: %s:%d\n", host, port);
    
    // 清理
    concord_config_destroy(cfg);
    
    return 0;
}
```

## 开发状态

当前正在实现基础的配置管理和通用工具函数，后续将添加监控指标和测试工具。

## 贡献

欢迎贡献代码、报告问题或提出新功能建议。详情请参阅 [贡献指南](../docs/CONTRIBUTING.md)。 