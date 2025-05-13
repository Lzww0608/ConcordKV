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
cd /path/to/ConcordKV/kvserver
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

## 2. 单元测试

### 2.1 测试范围

单元测试覆盖存储引擎的基本组件功能，包括：

- 基本键值操作（设置、获取、删除、修改）
- 数据结构实现（数组、红黑树、哈希表）
- 事务操作
- 持久化功能
- 并发控制
- 错误处理

### 2.2 运行单元测试

```bash
# 编译并运行所有单元测试
cd /path/to/ConcordKV/kvserver
make test

# 运行特定测试用例（例如仅测试红黑树实现）
./testcase --module=rbtree
```

### 2.3 测试用例设计

每个模块的测试用例应包括：

- **正常场景**：验证基本功能正常工作
- **边界场景**：测试极限值和边界条件
- **错误场景**：验证对错误输入的处理

### 2.4 内存泄漏检测

使用Valgrind检测内存泄漏：

```bash
valgrind --leak-check=full --show-leak-kinds=all ./testcase
```

## 3. 功能测试

### 3.1 持久化测试

测试WAL日志和快照机制的正确性：

1. **日志恢复测试**：
```bash
# 运行持久化测试
./testcase --module=persist --test=wal_recovery

# 模拟崩溃后恢复测试
./crash_recovery_test --wal-dir=/tmp/concordkv-test/logs
```

2. **快照创建与加载测试**：
```bash
./testcase --module=persist --test=snapshot
```

### 3.2 事务测试

验证事务的ACID特性：

1. **原子性测试**：
```bash
./testcase --module=transaction --test=atomicity
```

2. **一致性测试**：
```bash
./testcase --module=transaction --test=consistency
```

3. **隔离性测试**：
```bash
./testcase --module=transaction --test=isolation --level=repeatable_read
```

4. **持久性测试**：
```bash
./testcase --module=transaction --test=durability
```

### 3.3 并发控制测试

测试并发访问下的数据一致性：

1. **读写锁测试**：
```bash
./testcase --module=concurrency --test=rwlock --threads=8
```

2. **分段锁测试**：
```bash
./testcase --module=concurrency --test=segment_lock --segments=16 --threads=16
```

3. **死锁检测测试**：
```bash
./testcase --module=concurrency --test=deadlock_detection
```

### 3.4 错误处理测试

验证系统对各种错误的处理能力：

```bash
# 测试内存分配失败
./testcase --module=error --test=memory_allocation_failure

# 测试文件操作错误
./testcase --module=error --test=file_operation_failure

# 测试网络错误
./testcase --module=error --test=network_failure
```

## 4. 性能测试

### 4.1 基准测试

基准测试用于评估存储引擎的基本性能指标：

```bash
# 运行基准测试工具
cd /path/to/ConcordKV/tests
./benchmark --operations=1000000 --value-size=1024 --threads=1
```

关键性能指标包括：

- **吞吐量**：每秒操作数（ops/sec）
- **延迟**：操作的平均、最大和P99延迟
- **CPU使用率**：系统和用户CPU时间
- **内存占用**：峰值和平均内存使用

### 4.2 扩展性测试

测试系统在不同负载下的性能扩展性：

1. **数据量扩展性**：
```bash
./benchmark --key-count=1000000 --value-size=4096
```

2. **并发扩展性**：
```bash
for threads in 1 2 4 8 16 32 64; do
    ./benchmark --operations=1000000 --threads=$threads --duration=60
done
```

3. **读写比例测试**：
```bash
for write_ratio in 0.0 0.2 0.5 0.8 1.0; do
    ./benchmark --write-ratio=$write_ratio --duration=60
done
```

### 4.3 持久化性能测试

评估持久化对性能的影响：

```bash
# 无持久化基准
./benchmark --persist=none --duration=60

# 同步写入WAL
./benchmark --persist=sync --duration=60

# 异步写入WAL
./benchmark --persist=async --duration=60

# 定期快照
./benchmark --persist=snapshot --snapshot-interval=60 --duration=300
```

### 4.4 资源消耗测试

监控系统在不同工作负载下的资源消耗：

```bash
# 使用perf工具监控CPU使用情况
perf stat -d ./benchmark --duration=60

# 使用perf记录详细性能数据
perf record -g ./benchmark --duration=60
perf report

# 分析磁盘I/O
iostat -xm 5 | tee disk_io.log
```

## 5. 压力测试

### 5.1 长时间稳定性测试

验证系统在长期运行下的稳定性：

```bash
# 运行24小时持续测试
./stress-test --duration=24h --threads=8
```

### 5.2 极限负载测试

测试系统在极端条件下的行为：

```bash
# 高并发测试
./stress-test --threads=100 --duration=1h

# 大键值测试
./stress-test --value-size=10m --key-count=1000 --duration=1h

# 高频写入测试
./stress-test --write-ratio=1.0 --operations-per-second=10000 --duration=1h
```

### 5.3 故障恢复测试

验证系统在各种故障情况下的恢复能力：

```bash
# 随机崩溃测试
./crash-test --crash-probability=0.01 --duration=3h

# 磁盘空间耗尽测试
./disk-stress-test --fill-disk --duration=1h

# 网络分区测试
./network-partition-test --partition-interval=300 --duration=3h
```

### 5.4 资源限制测试

在资源受限的环境中测试系统性能：

```bash
# 限制CPU
taskset -c 0 ./benchmark --duration=10m

# 限制内存
cgroup-tools需要预先配置
cgexec -g memory:limited_mem ./benchmark --duration=10m
```

## 6. 集群测试

### 6.1 节点操作测试

测试集群节点的增加、删除和恢复：

```bash
# 启动3节点集群
./cluster-test --nodes=3 --start

# 添加节点测试
./cluster-test --add-node --host=192.168.1.4 --port=5000

# 删除节点测试
./cluster-test --remove-node --node-id=node-2

# 节点故障与恢复测试
./cluster-test --fail-node --node-id=node-3 --recover-after=60
```

### 6.2 领导者选举测试

验证Raft协议中的领导者选举功能：

```bash
# 领导者选举测试
./raft-test --test=leader_election --nodes=5

# 强制重选举测试
./raft-test --test=force_reelection --nodes=5
```

### 6.3 数据分片测试

测试数据分片和路由功能：

```bash
# 分片均衡测试
./shard-test --shards=64 --nodes=5 --test=balance

# 分片迁移测试
./shard-test --test=migration --source=node-1 --target=node-2

# 热点检测和处理测试
./shard-test --test=hotspot --duration=30m
```

### 6.4 一致性测试

验证分布式环境下的数据一致性：

```bash
# 线性一致性检查
./consistency-test --test=linearizability --operations=10000

# 部分分区下的一致性测试
./consistency-test --test=network_partition --duration=10m
```

## 7. 集成测试

### 7.1 与Raft服务器集成测试

测试存储引擎与Raft服务器的协同工作：

```bash
# 启动集成测试环境
cd /path/to/ConcordKV
./integration-test --components=kvserver,raftserver --nodes=3
```

### 7.2 与客户端库集成测试

验证存储引擎与客户端库的兼容性：

```bash
# Go客户端测试
cd /path/to/ConcordKV/tests
go test -v ./client_test.go

# C/C++客户端测试
make client_test
./client_test
```

### 7.3 端到端测试

执行完整的端到端测试场景：

```bash
# 端到端功能测试
./e2e-test --test=functionality

# 端到端性能测试
./e2e-test --test=performance

# 端到端可靠性测试
./e2e-test --test=reliability --duration=12h
```

## 8. 测试报告生成

### 8.1 测试结果收集

收集并整理测试结果：

```bash
# 收集单元测试结果
./collect_results.sh --type=unit

# 收集性能测试结果
./collect_results.sh --type=performance

# 收集压力测试结果
./collect_results.sh --type=stress
```

### 8.2 测试报告生成

根据测试结果生成报告：

```bash
# 生成综合测试报告
./generate_report.sh --output=test_report.html

# 生成性能对比报告
./generate_report.sh --type=performance_comparison --baseline=v1.0 --current=v1.1
```

### 8.3 测试覆盖率分析

分析代码覆盖率：

```bash
# 编译支持覆盖率分析的版本
make coverage

# 运行测试
./testcase_coverage

# 生成覆盖率报告
gcov *.c
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

## 9. 持续集成

### 9.1 CI配置

设置持续集成环境，在代码提交时自动运行测试：

```yaml
# Jenkins或GitLab CI配置示例
pipeline:
  stages:
    - build
    - unit_test
    - functional_test
    - performance_test
    - stress_test
    - integration_test
```

### 9.2 自动化测试脚本

示例自动化测试脚本：

```bash
#!/bin/bash
# 自动化测试脚本示例

set -e

# 编译
cd /path/to/ConcordKV/kvserver
make clean && make

# 运行单元测试
make test

# 运行功能测试
cd ../tests
./functional_tests.sh

# 运行简单性能测试
./performance_tests.sh --quick

# 生成报告
./generate_report.sh
```

## 10. 测试最佳实践

### 10.1 测试用例设计原则

- 测试用例应当独立且可重复
- 每个测试用例应专注于验证一个功能点
- 同时覆盖正常路径和异常路径
- 考虑边界条件和极端场景

### 10.2 性能测试注意事项

- 在测试前预热系统
- 每次测试使用相同的硬件和环境
- 多次重复测试取平均值
- 排除干扰因素（如系统后台任务）

### 10.3 测试环境维护

- 定期清理测试数据和日志
- 保持测试环境一致性
- 记录环境变更
- 使用容器或虚拟机隔离测试环境

## 附录

### A. 常见测试问题解决方案

- 测试失败排查流程
- 性能异常分析方法
- 内存泄漏定位技巧

### B. 测试命令参考

完整的测试命令参数说明和示例。

### C. 测试环境搭建脚本

自动化测试环境配置脚本和说明。 