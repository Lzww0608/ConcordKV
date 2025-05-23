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
    ├── README.md     - 主文档(本文件)
    ├── api_reference.md - API参考文档
    ├── architecture.md  - 架构设计文档
    └── examples.md   - 使用示例和最佳实践
```

## 核心特性

### 🔧 配置管理
- **多源配置**：支持YAML、JSON、INI等格式
- **类型安全**：强类型配置访问接口
- **热更新**：运行时配置变更通知
- **默认值**：优雅的默认值处理

### 🛠️ 通用工具
- **字符串操作**：分割、连接、替换等高效实现
- **文件操作**：读写、复制、目录管理
- **网络工具**：HTTP客户端、套接字操作
- **日志系统**：分级日志、结构化输出
- **时间工具**：高精度时间戳、格式化

### 📊 监控指标 (已完全实现)
- **四种指标类型**：Counter、Gauge、Histogram、Meter
- **线程安全**：无锁设计，高并发性能
- **多种输出格式**：文本、JSON、Prometheus
- **HTTP服务器**：内置指标暴露服务
- **百分位数统计**：P50、P90、P95、P99等
- **速率计算**：1分钟、5分钟、15分钟移动平均

### 🧪 测试工具 (已完全实现)
- **模拟客户端**：多线程负载生成，可配置读写比例
- **故障注入**：网络、CPU、内存等多种故障类型
- **基准测试**：详细性能分析和报告生成
- **数据生成**：支持模式和Zipf分布的测试数据
- **统计分析**：延迟分布、吞吐量、成功率等指标

## 快速开始

### 编译安装

```bash
# 克隆项目
git clone https://github.com/your-org/ConcordKV.git
cd ConcordKV/common

# 编译各个模块
make -C config
make -C util  
make -C metrics
make -C testing
```

### 基本使用

```c
#include "config/config.h"
#include "metrics/metrics.h"
#include "testing/testing.h"

int main() {
    // 加载配置
    concord_config_t *cfg = concord_config_load("app.yaml");
    
    // 创建指标仓库
    concord_metrics_repo_t *metrics = concord_metrics_repo_create(64);
    
    // 创建计数器
    concord_metric_t *counter = concord_metrics_create_counter(
        metrics, "requests_total", "请求总数", 0);
    
    // 使用指标
    concord_metrics_counter_inc(counter, 1);
    
    // 启动指标HTTP服务器  
    concord_metrics_start_server(metrics, "0.0.0.0", 8080, "/metrics");
    
    // 清理资源
    concord_config_destroy(cfg);
    concord_metrics_repo_destroy(metrics);
    
    return 0;
}
```

## 运行示例程序

### 监控指标演示

```bash
cd metrics
make
./metrics_demo
# 访问 http://localhost:8080/metrics 查看指标
```

### 测试工具演示

```bash
cd testing  
make
./testing_demo
# 观察模拟客户端、故障注入、基准测试等功能
```

## API参考

详细的API文档请参阅：

- [API参考文档](doc/api_reference.md) - 完整的API接口说明
- [架构设计文档](doc/architecture.md) - 系统架构和设计思路  
- [使用示例文档](doc/examples.md) - 详细示例和最佳实践

## 已实现功能状态

| 模块 | 状态 | 说明 |
|------|------|------|
| **metrics** | ✅ 完全实现 | 4种指标类型，HTTP服务器，多格式输出 |
| **testing** | ✅ 完全实现 | 模拟客户端，故障注入，基准测试，数据生成 |
| **doc** | ✅ 完全实现 | API文档，架构文档，示例文档 |
| config | 🔨 部分实现 | 基础框架已搭建，需完善解析器 |
| util | 🔨 部分实现 | 基础工具已实现，需扩展网络和日志 |

## 性能特性

### 监控指标性能
- **低延迟**：指标操作通常在微秒级别
- **高并发**：支持数千并发线程安全操作
- **内存效率**：哈希表存储，O(1)查找复杂度
- **快速导出**：支持多种格式的高效序列化

### 测试工具性能
- **高吞吐量**：单机可模拟数万QPS负载
- **精确统计**：微秒级延迟测量精度
- **资源控制**：可配置的内存和CPU使用限制
- **实时监控**：测试过程中的实时指标展示

## 线程安全

所有公共API都经过线程安全设计：

- **metrics**：使用原子操作，完全无锁
- **testing**：内部线程同步，外部调用安全
- **config**：读操作线程安全，写操作需外部同步
- **util**：大部分函数线程安全，具体见API文档

## 错误处理

统一的错误处理约定：

- 返回指针的函数：成功返回有效指针，失败返回`NULL`
- 返回整数的函数：成功返回`0`，失败返回`-1`  
- 获取值的函数：失败时返回默认值

## 示例场景

### 1. Web服务器监控

```c
// 创建HTTP请求相关指标
concord_metric_t *requests = concord_metrics_create_counter(repo, "http_requests_total", "HTTP请求总数", 0);
concord_metric_t *latency = concord_metrics_create_histogram(repo, "http_request_duration_ms", "请求延迟", &hist_config);

// 在请求处理中使用
void handle_request() {
    uint64_t start = get_time_us();
    
    // 处理请求...
    
    // 更新指标
    concord_metrics_counter_inc(requests, 1);
    concord_metrics_histogram_observe(latency, (get_time_us() - start) / 1000.0);
}
```

### 2. 数据库连接池监控

```c
// 创建连接池指标
concord_metric_t *active_conns = concord_metrics_create_gauge(repo, "db_connections_active", "活跃连接数", 0);
concord_metric_t *total_conns = concord_metrics_create_gauge(repo, "db_connections_total", "总连接数", 0);

// 连接获取/释放时更新
void acquire_connection() {
    concord_metrics_gauge_inc(active_conns, 1);
}

void release_connection() {
    concord_metrics_gauge_inc(active_conns, -1);
}
```

### 3. 性能基准测试

```c
// 创建基准测试
concord_benchmark_t *benchmark = concord_benchmark_create("数据库查询性能");
concord_benchmark_start(benchmark);

// 执行测试操作
for (int i = 0; i < 10000; i++) {
    uint64_t start_time = concord_benchmark_op_start(benchmark);
    
    // 执行数据库查询
    int success = execute_query();
    
    concord_benchmark_op_end(benchmark, start_time, success);
}

concord_benchmark_stop(benchmark);
concord_benchmark_print_report(benchmark);
```

## 贡献指南

欢迎贡献代码和提出改进建议！

1. Fork 项目仓库
2. 创建功能分支：`git checkout -b feature/new-feature`
3. 提交更改：`git commit -am 'Add new feature'`
4. 推送分支：`git push origin feature/new-feature`  
5. 创建 Pull Request

## 开发路线图

### 下一步计划
- [ ] 完善config模块的解析器实现
- [ ] 扩展util模块的网络和日志功能
- [ ] 添加更多测试用例和文档
- [ ] 性能优化和内存使用优化
- [ ] 支持更多配置格式和输出格式

### 长期目标
- [ ] 分布式配置中心集成
- [ ] 指标数据持久化存储
- [ ] 可视化监控界面
- [ ] 自动化故障检测和恢复

## 许可证

本项目采用 MIT 许可证 - 详情请参阅 [LICENSE](../LICENSE) 文件。

## 联系方式

- 项目主页：https://github.com/your-org/ConcordKV
- 问题反馈：https://github.com/your-org/ConcordKV/issues
- 技术讨论：https://github.com/your-org/ConcordKV/discussions 