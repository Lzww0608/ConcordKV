# ConcordKV 存储引擎测试指南

本文档提供了对ConcordKV存储引擎进行全面测试的详细指南，包括测试环境设置、测试类型、测试用例设计和执行方法。

## 1. 测试环境设置

### 1.1 测试环境需求

测试ConcordKV存储引擎需要以下环境：

- Linux操作系统（推荐Ubuntu 18.04+或CentOS 7+）
- GCC 7.0+或兼容的C/C++编译器
- 至少4GB可用RAM
- 至少10GB可用磁盘空间（用于持久化和快照测试）
- 对于集群测试，需要多台具有网络互通的服务器或虚拟机

### 1.2 测试工具

测试过程中需要使用以下工具：

- **Valgrind**：用于内存泄漏检测
- **perf**：用于性能分析
- **tcpdump/Wireshark**：用于网络通信分析
- **stress-ng**：用于系统压力测试

### 1.3 测试前准备

1. 编译存储引擎：
```bash
cd ConcordKV/kvserver
make clean && make
```

2. 准备测试目录：
```bash
mkdir -p /tmp/concordkv-test/{data,logs,snapshots}
```

3. 检查测试依赖：
```bash
# 安装Valgrind
sudo apt-get install valgrind

# 安装perf
sudo apt-get install linux-tools-common linux-tools-generic

# 安装stress-ng
sudo apt-get install stress-ng
```

## 2. 测试套件概览

ConcordKV提供了完整的测试套件，按功能分类组织：

```
tests/
├── enhanced_persistence/          # 增强持久化功能测试
├── unit_tests/                    # 单元测试
├── performance_tests/             # 性能基准测试
├── integration_tests/             # 集成测试
└── run_all_tests.sh              # 主测试脚本
```

### 2.1 快速运行所有测试

```bash
cd ConcordKV/tests
./run_all_tests.sh
```

### 2.2 运行特定类型的测试

```bash
# 只运行单元测试
./run_all_tests.sh --unit-only

# 只运行增强持久化功能测试
./run_all_tests.sh --enhanced-only

# 只运行性能测试
./run_all_tests.sh --perf-only

# 运行所有测试包含内存检查
./run_all_tests.sh --with-valgrind
```

## 3. 单元测试

### 3.1 测试范围

单元测试覆盖存储引擎的基本组件功能，包括：

- 基本键值操作（设置、获取、删除、修改）
- 数据结构实现（数组、红黑树、哈希表）
- 事务操作
- 持久化功能
- 并发控制
- 错误处理

### 3.2 运行单元测试

```bash
cd ConcordKV/tests/unit_tests
make clean && make test
```

### 3.3 测试用例设计

每个模块的测试用例应包括：

- **正常场景**：验证基本功能正常工作
- **边界场景**：测试极限值和边界条件
- **错误场景**：验证对错误输入的处理

### 3.4 内存泄漏检测

使用Valgrind检测内存泄漏：

```bash
cd ConcordKV/tests/unit_tests
valgrind --leak-check=full --show-leak-kinds=all ./simple_test
```

## 4. 增强持久化功能测试

### 4.1 测试内容

增强持久化功能测试包括：

- WAL日志压缩测试
- 增量持久化测试
- 日志轮转测试
- 增量快照测试
- 并发操作测试
- 性能测试

### 4.2 运行测试

```bash
cd ConcordKV/tests/enhanced_persistence
./run_enhanced_tests.sh
```

### 4.3 详细测试项目

1. **日志压缩测试**：
   - 验证日志压缩功能正确性
   - 测试压缩后的空间节省效果
   - 验证压缩后数据的完整性

2. **增量持久化测试**：
   - 测试批量同步功能
   - 验证定时同步机制
   - 测试崩溃恢复能力

3. **日志轮转测试**：
   - 验证文件大小限制
   - 测试多文件管理
   - 验证旧文件清理

4. **增量快照测试**：
   - 测试增量快照创建
   - 验证增量快照恢复
   - 测试快照文件管理

## 5. 性能基准测试

### 5.1 基准测试内容

性能基准测试评估以下指标：

- **吞吐量**：每秒操作数（ops/sec）
- **延迟**：操作的平均、最大和P99延迟
- **CPU使用率**：系统和用户CPU时间
- **内存占用**：峰值和平均内存使用

### 5.2 运行基准测试

```bash
cd ConcordKV/tests/performance_tests
make clean && make benchmark
```

### 5.3 扩展性测试

测试系统在不同负载下的性能扩展性：

1. **数据量扩展性**：
```bash
# 测试大数据量下的性能
./persistence_benchmark --operations=1000000
```

2. **并发扩展性**：
```bash
# 测试多线程并发性能
./persistence_benchmark --threads=8
```

### 5.4 性能优化建议

基于测试结果的性能优化建议：

- **异步写入**: 在高吞吐量场景下使用异步写入
- **批量操作**: 使用批量操作减少系统调用开销
- **内存预分配**: 预分配内存减少动态分配
- **压缩配置**: 根据存储空间需求调整压缩参数

## 6. 集成测试

### 6.1 测试范围

集成测试验证多个组件之间的协作：

- 存储引擎与Raft服务器的集成
- 客户端与服务器的通信
- 集群节点间的数据同步
- 故障恢复和容错能力

### 6.2 集群测试

1. **多节点集群测试**：
```bash
# 启动3节点集群进行测试
cd ConcordKV/raftserver
./test_cluster.sh
```

2. **故障恢复测试**：
```bash
# 模拟节点故障和恢复
./test_failover.sh
```

### 6.3 客户端集成测试

```bash
cd ConcordKV/client/go
go test -v ./...
```

## 7. 压力测试

### 7.1 长时间运行测试

验证系统在长时间运行下的稳定性：

```bash
# 运行24小时压力测试
./stress_test.sh --duration=24h --operations=continuous
```

### 7.2 高并发测试

测试系统在高并发访问下的表现：

```bash
# 高并发读写测试
./concurrent_test.sh --clients=100 --duration=1h
```

### 7.3 资源限制测试

在资源受限环境下测试系统行为：

```bash
# 限制内存测试
systemd-run --scope -p MemoryLimit=1G ./memory_limit_test.sh

# 限制CPU测试
systemd-run --scope -p CPUQuota=50% ./cpu_limit_test.sh
```

## 8. 故障注入测试

### 8.1 网络故障模拟

```bash
# 模拟网络延迟
tc qdisc add dev eth0 root netem delay 100ms

# 模拟网络丢包
tc qdisc add dev eth0 root netem loss 1%
```

### 8.2 磁盘故障模拟

```bash
# 模拟磁盘满
dd if=/dev/zero of=/tmp/fill_disk bs=1M count=1000

# 模拟磁盘I/O错误
echo 1 > /sys/block/sda/queue/iostats
```

### 8.3 内存压力测试

```bash
# 使用stress-ng进行内存压力测试
stress-ng --vm 4 --vm-bytes 1G --timeout 60s
```

## 9. 测试报告和分析

### 9.1 自动化测试报告

测试脚本会自动生成详细的测试报告：

```bash
# 查看最新的测试报告
cat ConcordKV/tests/test_summary_report.md
```

### 9.2 性能分析

使用perf工具进行性能分析：

```bash
# 性能分析
perf record -g ./persistence_benchmark
perf report
```

### 9.3 内存分析

使用Valgrind进行内存分析：

```bash
# 内存使用分析
valgrind --tool=massif ./simple_test
ms_print massif.out.*
```

## 10. 持续集成

### 10.1 自动化测试流程

建议的CI/CD流程：

1. **代码提交触发**: 每次代码提交自动运行单元测试
2. **夜间构建**: 运行完整的测试套件包括性能测试
3. **发布前验证**: 运行所有测试确保质量

### 10.2 测试环境管理

- **隔离环境**: 每个测试运行在独立的环境中
- **数据清理**: 测试后自动清理临时数据
- **资源监控**: 监控测试过程中的资源使用

## 11. 故障排除

### 11.1 常见问题

1. **编译错误**: 检查依赖项和编译器版本
2. **测试超时**: 调整超时参数或检查系统负载
3. **内存泄漏**: 使用Valgrind定位内存泄漏位置
4. **性能下降**: 检查系统资源和配置参数

### 11.2 调试技巧

- 使用GDB进行调试：`gdb ./simple_test`
- 启用详细日志：设置环境变量`DEBUG=1`
- 使用strace跟踪系统调用：`strace -o trace.log ./simple_test`

---

本测试指南提供了ConcordKV存储引擎的全面测试方法。通过系统性的测试，可以确保系统的正确性、性能和可靠性。建议在开发过程中定期运行测试，并在发布前进行完整的测试验证。 