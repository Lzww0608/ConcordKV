# ConcordKV Common 模块

ConcordKV 分布式键值存储系统的通用工具和基础设施模块。

## 概述

本模块为 ConcordKV 系统提供核心的基础设施支持，包括配置管理、通用工具、监控指标和测试框架。所有组件都经过精心设计，具有高性能、线程安全和易于使用的特点。

## 目录结构

```
common/
├── config/           - 配置管理模块
│   ├── config.h/.c   - 核心配置引擎
│   └── Makefile      - 编译配置
├── util/             - 通用工具模块  
│   ├── util.h/.c     - 基础工具函数
│   ├── log.h/.c      - 日志系统
│   ├── network.h/.c  - 网络工具
│   ├── network_io.c  - 网络IO扩展
│   ├── timer.h/.c    - 定时器
│   ├── crypt.h/.c    - 加密工具
│   └── Makefile      - 编译配置
├── metrics/          - 监控指标模块 ✅ 完全实现
│   ├── metrics.h/.c  - 核心指标引擎
│   ├── metrics_format.c - 格式化输出和HTTP服务
│   ├── metrics_demo.c   - 示例程序
│   └── Makefile      - 编译配置
├── testing/          - 测试工具模块 ✅ 完全实现
│   ├── testing.h     - 统一接口定义
│   ├── mock_client.c - 模拟客户端
│   ├── fault_inject.c - 故障注入
│   ├── benchmark.c   - 性能基准测试
│   ├── test_data.c   - 测试数据生成
│   ├── testing_demo.c - 综合示例程序
│   └── Makefile      - 编译配置
└── doc/              - 文档目录 ✅ 完全实现
    ├── api_reference.md - API参考文档
    ├── architecture.md  - 架构设计文档
    └── examples.md   - 使用示例和最佳实践
```

## 核心特性

### 🔧 配置管理
- **多源配置**：支持YAML、JSON、INI等格式
- **类型安全**：强类型配置访问接口
- **动态更新**：支持运行时配置热更新
- **验证机制**：配置项合法性验证

### 🛠️ 通用工具
- **字符串处理**：高效的字符串操作函数
- **日志系统**：结构化日志，支持多级别和轮转
- **网络工具**：TCP/UDP通信和地址解析
- **加密工具**：常用加密算法和哈希函数
- **定时器**：高精度定时器和调度器

### 📊 监控指标
- **多种指标类型**：计数器、仪表盘、直方图
- **实时统计**：低延迟的指标收集和聚合
- **格式化输出**：支持Prometheus格式
- **HTTP服务**：内置HTTP指标服务器
- **线程安全**：完全的多线程支持

### 🧪 测试工具
- **模拟客户端**：可配置的负载生成器
- **故障注入**：网络、磁盘、内存故障模拟
- **性能基准**：详细的性能测试框架
- **数据生成**：多种模式的测试数据生成
- **自动化测试**：完整的测试自动化支持

## 快速开始

### 编译所有模块

```bash
cd ConcordKV/common
make all
```

### 运行示例程序

```bash
# 配置管理示例
cd config && make demo && ./config_demo

# 监控指标示例
cd metrics && make demo && ./metrics_demo

# 测试工具示例
cd testing && make demo && ./testing_demo
```

### 基本使用示例

```c
#include "config/config.h"
#include "util/util.h"
#include "metrics/metrics.h"

int main() {
    // 配置管理
    concord_config_t *cfg = concord_config_load("app.yaml");
    int port = concord_config_get_int(cfg, "server.port", 8080);
    
    // 日志记录
    concord_util_info("服务器启动，端口: %d", port);
    
    // 指标监控
    concord_metrics_repo_t *metrics = concord_metrics_repo_create(32);
    concord_metric_t *counter = concord_metrics_create_counter(
        metrics, "requests_total", "总请求数", 0);
    
    // 业务逻辑...
    concord_metrics_counter_inc(counter, 1);
    
    // 清理资源
    concord_metrics_repo_destroy(metrics);
    concord_config_destroy(cfg);
    
    return 0;
}
```

## 性能特性

- **低延迟**：指标操作延迟 < 100ns
- **高吞吐**：支持百万级QPS的指标更新
- **内存效率**：优化的内存使用和缓存友好设计
- **线程安全**：无锁数据结构和细粒度锁
- **可扩展**：支持大规模分布式部署

## 文档

- [API参考](doc/api_reference.md) - 完整的API文档
- [架构设计](doc/architecture.md) - 详细的架构说明
- [使用示例](doc/examples.md) - 丰富的使用示例和最佳实践

## 开发指南

### 添加新功能

1. 在相应模块目录下创建源文件
2. 更新头文件中的接口定义
3. 添加相应的测试用例
4. 更新Makefile和文档

### 编译选项

```bash
# 调试版本
make DEBUG=1

# 发布版本
make RELEASE=1

# 启用所有警告
make STRICT=1

# 生成覆盖率报告
make COVERAGE=1
```

### 测试

```bash
# 运行所有测试
make test

# 运行特定模块测试
make test-config
make test-metrics
make test-testing

# 内存检查
make valgrind
```

## 贡献

欢迎贡献代码和改进建议！请遵循以下准则：

1. **代码风格**：遵循项目的编码规范
2. **测试覆盖**：新功能必须包含测试用例
3. **文档更新**：更新相关的API文档和示例
4. **性能考虑**：关注性能影响，避免引入性能回归

## 许可证

本模块采用 MIT 许可证，与主项目保持一致。

---

**ConcordKV Common** - 为分布式系统提供可靠的基础设施支持 