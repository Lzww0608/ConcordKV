# ConcordKV 增强持久化功能

本文档介绍了 ConcordKV 项目中新增的增强持久化功能，包括日志压缩和增量持久化等特性。

## 🚀 新增功能概览

### 1. 日志压缩 (Log Compaction)
- **自动压缩**: 当WAL日志条目超过阈值时自动触发压缩
- **空间优化**: 移除冗余的历史操作，只保留最新状态
- **后台处理**: 压缩过程在后台线程中进行，不影响正常操作
- **可配置**: 支持自定义压缩阈值和比例

### 2. 增量持久化 (Incremental Persistence)
- **批量同步**: 支持批量写入后再同步到磁盘
- **定时同步**: 可配置的定时同步间隔
- **性能优化**: 减少磁盘I/O操作，提高写入性能
- **数据安全**: 保证数据的最终一致性

### 3. 日志轮转 (Log Rotation)
- **文件大小控制**: 当日志文件超过指定大小时自动轮转
- **文件管理**: 自动管理多个日志文件
- **历史清理**: 支持清理旧的日志文件

### 4. 增量快照 (Incremental Snapshots)
- **增量备份**: 只备份变更的数据
- **快速恢复**: 支持从增量快照快速恢复
- **存储优化**: 减少快照文件大小

### 5. 性能监控 (Performance Monitoring)
- **实时统计**: 提供写入、同步、压缩等操作的性能统计
- **延迟监控**: 监控各种操作的延迟情况
- **吞吐量统计**: 统计系统的吞吐量指标

## 📁 文件结构

```
ConcordKV/tests/
├── enhanced_persistence_test.c     # 功能测试文件
├── persistence_benchmark.c         # 性能基准测试
├── Makefile.enhanced               # 编译配置
├── run_enhanced_tests.sh           # 自动化测试脚本
└── README_ENHANCED.md              # 本文档

ConcordKV/kvserver/
├── kv_persist.h                    # 增强的持久化头文件
├── kv_persist_enhanced.c           # 增强的WAL实现
└── kv_snapshot_enhanced.c          # 增强的快照实现
```

## 🔧 编译和运行

### 前置要求
- GCC 编译器 (支持 C99 标准)
- pthread 库
- make 工具
- 可选: valgrind (用于内存检查)

### 快速开始

1. **运行基本功能测试**:
```bash
cd ConcordKV/tests
./run_enhanced_tests.sh
```

2. **运行性能基准测试**:
```bash
./run_enhanced_tests.sh --benchmark
```

3. **运行内存检查**:
```bash
./run_enhanced_tests.sh --valgrind
```

### 手动编译

```bash
# 编译功能测试
make -f Makefile.enhanced all

# 运行测试
./enhanced_persistence_test

# 编译性能基准测试
gcc -Wall -Wextra -std=c99 -g -O2 -pthread -I../kvserver \
    -o persistence_benchmark \
    persistence_benchmark.c \
    ../kvserver/kv_persist_enhanced.c \
    ../kvserver/kv_snapshot_enhanced.c \
    -lpthread

# 运行基准测试
./persistence_benchmark
```

## 📊 API 使用示例

### 基本WAL操作

```c
#include "kv_persist.h"

// 初始化WAL
wal_t wal;
wal_init(&wal, "/path/to/logs", 1); // 1表示同步写入

// 添加日志条目
wal_append(&wal, WAL_OP_SET, "key1", "value1");
wal_append(&wal, WAL_OP_DEL, "key2", NULL);

// 强制同步
wal_force_sync(&wal);

// 清理
wal_destroy(&wal);
```

### 日志压缩

```c
// 启动自动压缩
wal_start_compaction(&wal, engine, get_all_keys_callback);

// 手动触发压缩
wal_compact_logs(&wal, engine, get_all_keys_callback, get_value_callback);

// 清理旧日志
wal_cleanup_old_logs(&wal, before_sequence_number);
```

### 增量同步

```c
// 启动增量同步线程
wal_start_incremental_sync(&wal, engine);

// 手动增量同步
wal_incremental_sync(&wal, engine);
```

### 快照操作

```c
snapshot_t snap;
snapshot_init(&snap, "/path/to/snapshots");

// 创建完整快照
snapshot_create(&snap, engine, save_data_callback);

// 创建增量快照
snapshot_create_incremental(&snap, engine, from_seq, to_seq, save_incremental_callback);

// 加载快照
snapshot_load(&snap, engine, load_data_callback);

// 应用增量快照
snapshot_apply_incremental(&snap, engine, apply_incremental_callback);

snapshot_destroy(&snap);
```

## ⚙️ 配置参数

### WAL配置
```c
#define WAL_COMPACT_THRESHOLD    1000    // 触发压缩的日志条目数
#define WAL_COMPACT_RATIO        0.3     // 压缩比例阈值
#define WAL_MAX_FILE_SIZE        (64 * 1024 * 1024)  // 最大文件大小 64MB
```

### 增量同步配置
```c
#define INCREMENTAL_SYNC_INTERVAL  5     // 增量同步间隔(秒)
#define INCREMENTAL_BATCH_SIZE     100   // 增量批处理大小
```

## 🧪 测试覆盖

### 功能测试
- ✅ 基本WAL功能
- ✅ 日志轮转
- ✅ 日志压缩
- ✅ 增量同步
- ✅ 快照创建和恢复
- ✅ 增量快照
- ✅ 并发操作
- ✅ 错误处理

### 性能测试
- ✅ 单线程写入性能
- ✅ 多线程并发性能
- ✅ 同步 vs 异步性能对比
- ✅ 内存使用监控
- ✅ 延迟分析 (平均值和P99)

### 压力测试
- ✅ 大量数据写入
- ✅ 长时间运行稳定性
- ✅ 内存泄漏检查
- ✅ 并发安全性

## 📈 性能特性

### 预期性能指标
- **写入吞吐量**: 10,000+ ops/sec (异步模式)
- **同步延迟**: < 1ms (平均)
- **压缩效率**: 30-70% 空间节省
- **内存使用**: < 100MB (正常负载)

### 性能优化建议
1. **异步模式**: 对于高吞吐量场景，使用异步写入模式
2. **批量操作**: 使用增量同步减少磁盘I/O
3. **定期压缩**: 配置合适的压缩阈值
4. **内存管理**: 定期清理旧的日志文件

## 🔍 故障排除

### 常见问题

1. **编译错误**:
   - 确保安装了 pthread 库
   - 检查 GCC 版本是否支持 C99

2. **测试超时**:
   - 检查系统负载
   - 增加超时时间配置

3. **内存泄漏**:
   - 使用 valgrind 检查
   - 确保正确调用 destroy 函数

4. **性能问题**:
   - 检查磁盘I/O性能
   - 调整同步策略
   - 优化压缩参数

### 调试技巧

1. **启用调试模式**:
```bash
make -f Makefile.enhanced debug
```

2. **查看详细日志**:
```bash
export DEBUG=1
./enhanced_persistence_test
```

3. **内存检查**:
```bash
valgrind --leak-check=full ./enhanced_persistence_test
```

## 🤝 贡献指南

### 代码规范
- 遵循 C99 标准
- 使用一致的命名约定
- 添加适当的注释
- 包含单元测试

### 提交流程
1. Fork 项目
2. 创建功能分支
3. 编写测试
4. 提交 Pull Request

### 测试要求
- 所有新功能必须包含测试
- 测试覆盖率 > 90%
- 通过内存检查
- 性能回归测试

## 📚 参考资料

- [ConcordKV 架构文档](../ARCHITECTURE.md)
- [开发路线图](../docs/ROADMAP.md)
- [贡献指南](../CONTRIBUTING.md)

## 📄 许可证

本项目采用 MIT 许可证，详见 LICENSE 文件。

---

**注意**: 这是一个开发中的功能，API 可能会发生变化。建议在生产环境使用前进行充分测试。 