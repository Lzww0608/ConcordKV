# ConcordKV 测试套件

本目录包含 ConcordKV 项目的所有测试文件，按功能和类型进行分类组织。

## 📁 目录结构

```
tests/
├── enhanced_persistence/          # 增强持久化功能测试
│   ├── enhanced_persistence_test.c    # 完整功能测试
│   ├── Makefile.enhanced              # 构建配置
│   ├── run_enhanced_tests.sh          # 自动化测试脚本
│   └── README_ENHANCED.md             # 详细文档
├── distributed_transaction_tests/ # 分布式事务测试 🆕
│   ├── test_distributed_transaction.c # 分布式事务核心测试
│   ├── test_priority_queue.c          # 优先级队列测试
│   ├── test_2pc_protocol.c            # 2PC协议测试
│   ├── test_timeout_handling.c        # 超时处理测试
│   ├── Makefile                       # 构建系统
│   ├── run_tests.sh                   # 自动化测试脚本
│   └── distributed_transaction_test_report.md # 测试报告
├── unit_tests/                    # 单元测试
│   └── simple_test                    # 基本功能单元测试
├── integration_tests/             # 集成测试
│   ├── kvserver_tests/               # KV服务器集成测试
│   ├── raftserver/                   # Raft服务器集成测试
│   └── client/                       # 客户端集成测试
├── performance_tests/             # 性能测试
│   └── persistence_benchmark.c       # 持久化性能基准测试
└── README.md                      # 本文件
```

## 🧪 测试分类说明

### 1. 增强持久化功能测试 (`enhanced_persistence/`)
- **目的**: 测试新增的日志压缩、增量持久化等高级功能
- **特点**: 全面的功能测试，包含并发、性能、错误处理等场景
- **运行方式**: `cd enhanced_persistence && ./run_enhanced_tests.sh`

### 2. 分布式事务测试 (`distributed_transaction_tests/`) 🆕
- **目的**: 测试分布式事务系统的完整功能
- **特点**: 
  - **2PC协议**: 两阶段提交协议的完整测试
  - **优先级调度**: 事务优先级调度系统测试
  - **超时处理**: 多层次超时检测和处理测试
  - **并发控制**: 高并发场景下的事务管理测试
  - **故障恢复**: 网络分区和节点故障恢复测试
- **测试覆盖**: 212个测试用例，96.7%成功率
- **运行方式**: `cd distributed_transaction_tests && ./run_tests.sh --mode all`

### 3. 单元测试 (`unit_tests/`)
- **目的**: 测试单个模块或函数的基本功能
- **特点**: 快速、独立、专注于特定功能点
- **运行方式**: 直接执行各个测试程序

### 4. 集成测试 (`integration_tests/`)
- **目的**: 测试多个组件之间的协作
- **特点**: 模拟真实使用场景，测试组件间接口
- **运行方式**: 按子目录分别运行

### 5. 性能测试 (`performance_tests/`)
- **目的**: 评估系统性能指标
- **特点**: 大量数据、长时间运行、详细的性能统计
- **运行方式**: 需要较长时间，建议在性能测试环境运行

## 🚀 快速开始

### 运行所有测试
```bash
# 运行增强持久化功能测试
cd enhanced_persistence
./run_enhanced_tests.sh

# 运行单元测试
cd ../unit_tests
./simple_test

# 运行性能基准测试
cd ../performance_tests
gcc -Wall -Wextra -std=c99 -g -O2 -pthread -I../../kvserver \
    -o persistence_benchmark \
    persistence_benchmark.c \
    ../../kvserver/kv_persist_enhanced.c \
    ../../kvserver/kv_snapshot_enhanced.c \
    -lpthread
./persistence_benchmark
```

### 运行特定类型的测试
```bash
# 只运行功能测试（快速）
cd enhanced_persistence
./run_enhanced_tests.sh

# 运行性能基准测试
cd enhanced_persistence
./run_enhanced_tests.sh --benchmark

# 运行内存检查
cd enhanced_persistence
./run_enhanced_tests.sh --valgrind
```

## 📊 测试覆盖范围

### 功能覆盖
- ✅ WAL基本操作（写入、恢复）
- ✅ 日志轮转和文件管理
- ✅ 日志压缩和空间优化
- ✅ 增量持久化和同步
- ✅ 快照创建和恢复
- ✅ 增量快照功能
- ✅ 并发操作安全性
- ✅ 错误处理和边界条件

### 性能覆盖
- ✅ 写入吞吐量测试
- ✅ 延迟分析（平均值和P99）
- ✅ 多线程并发性能
- ✅ 内存使用监控
- ✅ 同步vs异步性能对比

### 质量保证
- ✅ 内存泄漏检查（valgrind）
- ✅ 线程安全性验证
- ✅ 长时间运行稳定性
- ✅ 异常情况处理

## 🔧 开发指南

### 添加新测试
1. 确定测试类型（单元/集成/性能）
2. 在相应目录创建测试文件
3. 遵循现有的命名约定
4. 添加适当的超时机制
5. 更新相关的README文档

### 测试规范
- 使用 `assert()` 进行断言检查
- 设置合理的超时时间（防止测试卡死）
- 清理测试产生的临时文件
- 提供清晰的测试输出信息
- 支持自动化运行

### 性能测试注意事项
- 在专用的性能测试环境运行
- 关闭不必要的系统服务
- 多次运行取平均值
- 记录系统配置信息

## 📈 持续集成

### 自动化测试流程
1. **代码提交触发**: 每次代码提交自动运行基础测试
2. **夜间构建**: 运行完整的测试套件包括性能测试
3. **发布前验证**: 运行所有测试确保质量

### 测试报告
- 测试结果自动生成报告
- 性能指标趋势分析
- 代码覆盖率统计
- 内存使用情况监控

## 🤝 贡献指南

### 提交测试
1. 确保新功能包含相应测试
2. 运行现有测试确保无回归
3. 更新相关文档
4. 提交Pull Request

### 测试质量要求
- 测试覆盖率 > 90%
- 无内存泄漏
- 支持并发场景
- 包含错误处理测试

---

**注意**: 运行性能测试可能需要较长时间，建议在专用的测试环境中进行。如有问题，请参考各子目录中的详细文档。 