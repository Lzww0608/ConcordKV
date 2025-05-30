# ConcordKV

ConcordKV 是一个基于 Raft 协议的分布式键值存储系统，旨在提供高可用性、强一致性和良好的扩展性。系统由 C/C++ 实现的存储引擎和 Go 实现的分布式协调层组成。

## 项目结构

```
ConcordKV/
├── ARCHITECTURE.md         - 系统架构文档
├── README.md               - 项目说明文档（本文件）
├── kvserver/               - C/C++实现的KV存储引擎
├── raftserver/             - Go实现的Raft共识服务
├── client/                 - 客户端库
├── common/                 - 通用工具和配置
├── docs/                   - 详细文档
└── tests/                  - 测试套件
    ├── enhanced_persistence/   - 增强持久化功能测试
    ├── distributed_transaction_tests/ - 分布式事务测试 🆕
    ├── unit_tests/            - 单元测试
    ├── performance_tests/     - 性能基准测试
    └── integration_tests/     - 集成测试
```

## 🚀 功能特性

### 核心存储引擎
- **多种存储结构**: 支持数组、红黑树、哈希表等多种数据结构
- **增强持久化**: WAL日志压缩、增量持久化、自动日志轮转
- **并发控制**: 读写锁、自旋锁和分段锁等多级并发控制
- **事务支持**: 完整的ACID特性和多种隔离级别
- **快照系统**: 支持完整快照和增量快照

### 分布式事务系统 🆕
- **2PC协议**: 完整的两阶段提交协议实现
- **分布式协调器**: 支持多节点事务协调和管理
- **优先级调度**: 基于优先级的事务调度系统
- **超时处理**: 多层次超时检测和死锁预防机制
- **故障恢复**: 网络分区检测和自动恢复
- **并发控制**: 线程安全的分布式事务处理

### 分布式协调
- **Raft协议**: 完整的Raft一致性协议实现
- **集群管理**: 节点发现、健康检查和自动故障转移
- **数据分片**: 智能分片策略提高系统扩展性
- **负载均衡**: 多种负载均衡策略和自动故障转移

### 客户端支持
- **多语言客户端**: Go和C++客户端库
- **批量操作**: 支持批量读写操作
- **事务隔离**: 支持多种事务隔离级别
- **连接管理**: 自动重连和连接池管理

### 监控和运维
- **性能监控**: 实时性能指标和延迟统计
- **健康检查**: 集群健康状态监控
- **日志系统**: 详细的操作日志和错误处理
- **测试工具**: 完整的测试套件和性能基准测试

## 🏃 快速开始

### 环境要求

- **操作系统**: Linux (推荐Ubuntu 18.04+)
- **编译器**: GCC 7.0+ 或兼容的C/C++编译器
- **Go版本**: Go 1.16+ (用于Raft服务器和客户端)
- **内存**: 至少2GB可用内存
- **磁盘**: 至少5GB可用空间

### 编译

```bash
# 编译存储引擎
cd kvserver
make

# 编译Raft服务器
cd ../raftserver
go build

# 编译客户端库
cd ../client/go
go build
```

### 运行测试

```bash
# 运行完整测试套件
cd tests
./run_all_tests.sh

# 只运行单元测试
./run_all_tests.sh --unit-only

# 运行性能基准测试
./run_all_tests.sh --perf-only
```

### 启动服务

```bash
# 单节点模式
cd raftserver
./concord_raft -config single_node.yaml

# 集群模式（需要3个终端）
./concord_raft -config node1.yaml
./concord_raft -config node2.yaml  
./concord_raft -config node3.yaml
```

### 客户端使用

```go
package main

import (
    "fmt"
    "log"
    "time"
    "github.com/concordkv/client/concord"
)

func main() {
    // 创建客户端
    config := concord.Config{
        Endpoints: []string{"127.0.0.1:8081"},
        Timeout:   3 * time.Second,
    }
    
    client, err := concord.NewClient(config)
    if err != nil {
        log.Fatal(err)
    }
    defer client.Close()
    
    // 基本操作
    client.Set("greeting", "Hello, ConcordKV!")
    value, _ := client.Get("greeting")
    fmt.Println("Value:", value)
}
```

## 📊 性能特性

基于我们的基准测试结果：

- **写入吞吐量**: 10,000+ ops/sec (异步模式)
- **读取延迟**: < 1ms (平均)
- **存储优化**: 日志压缩节省30-70%空间
- **并发支持**: 良好的多线程扩展性
- **内存使用**: 稳定，无内存泄漏

## 📚 文档

- [系统架构](ARCHITECTURE.md) - 详细的系统架构设计
- [开发计划](docs/ROADMAP.md) - 项目开发路线图
- [API参考](docs/kvserver-api-reference.md) - 完整的API文档
- [测试指南](tests/README.md) - 测试套件使用指南
- [增强持久化](tests/COMPLETION_SUMMARY.md) - 最新功能完成总结

## 🤝 贡献指南

我们欢迎社区贡献！请参阅以下指南：

1. **报告问题**: 使用GitHub Issues报告bug或提出功能请求
2. **代码贡献**: Fork项目，创建feature分支，提交Pull Request
3. **文档改进**: 帮助改进文档和示例代码
4. **测试**: 添加测试用例，提高代码覆盖率

### 开发环境设置

```bash
# 克隆项目
git clone https://github.com/your-org/ConcordKV.git
cd ConcordKV

# 安装依赖
sudo apt-get install build-essential golang-go

# 运行测试确保环境正常
cd tests && ./run_all_tests.sh --unit-only
```

## 📈 项目状态

### ✅ 已完成功能
- [x] 核心存储引擎 (数组、红黑树、哈希表)
- [x] 增强持久化机制 (日志压缩、增量持久化)
- [x] 完整的Raft协议实现
- [x] 事务支持和并发控制
- [x] 分布式事务系统 (2PC协议、优先级调度、超时处理) 🆕
- [x] 死锁检测和预防 🆕
- [x] Go客户端库
- [x] 完整的测试套件

### 🚧 进行中
- [ ] C++客户端库
- [ ] 性能优化和基准测试
- [ ] 可视化管理界面

### 🔮 计划中
- [ ] 跨数据中心复制
- [ ] 更多编程语言客户端
- [ ] 安全性增强
- [ ] 智能分片和负载均衡

## 📄 许可证

本项目采用 MIT 许可证，详情请参阅 [LICENSE](LICENSE) 文件。

---

**ConcordKV** - 高性能分布式键值存储系统

