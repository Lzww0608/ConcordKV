# ConcordKV

**高性能分布式键值存储系统**

ConcordKV是一个使用C语言实现的高性能、分布式键值存储系统，目标是提供ACID事务支持、多种存储引擎、强一致性保证和水平扩展能力。

## 🌟 核心特性

### 🎯 多种存储引擎
- **Array存储引擎** - 适用于小规模数据和快速原型
- **Hash存储引擎** - 高性能哈希表实现，支持O(1)查找
- **RBTree存储引擎** - 红黑树实现，保证O(log n)有序操作
- **B+Tree存储引擎** ⭐ - **新增**高性能存储引擎，支持范围查询和高并发

### 🔄 分布式事务
- **两阶段提交(2PC)协议** - 保证分布式事务的ACID特性
- **优先级调度系统** - 智能事务调度，避免死锁
- **超时检测与恢复** - 自动检测和恢复故障事务
- **网络故障容错** - 分区容忍性设计

### 🚀 高性能与并发
- **无锁设计** - 关键路径采用无锁数据结构
- **多级锁机制** - 粒度可控的并发控制
- **内存池管理** - 减少内存分配开销
- **零拷贝I/O** - 高效的数据传输

### 📊 生产级功能
- **WAL日志** - 写前日志保证数据持久性
- **快照备份** - 支持增量和全量备份
- **集群管理** - 自动故障检测和恢复
- **监控指标** - 丰富的性能监控数据

## 📈 性能指标

### B+Tree存储引擎 ⭐ **主要推荐**
- **吞吐量**: 832,484 ops/sec (8线程并发)
- **单线程性能**: 105,000-110,000 ops/sec  
- **内存效率**: 仅4-5%额外开销
- **并发安全**: 稳定的线程安全性
- **延迟**: 亚毫秒级响应

### Hash存储引擎
- **写入性能**: 1,460,000 ops/sec
- **读取性能**: O(1)平均复杂度
- **内存使用**: 动态扩展

## 🏗️ 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (Client)                      │
├─────────────────────────────────────────────────────────┤
│                  统一引擎接口层                        │
│  ┌─────────────────────────────────────────────────────┐│
│  │ kv_engine_interface.h - 统一存储引擎接口           ││
│  │ kv_engine_factory.c - 存储引擎工厂模式              ││
│  └─────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────┤
│                   存储引擎层                           │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────┐   │
│  │ Array   │ │  Hash   │ │ RBTree  │ │ B+Tree ⭐   │   │
│  │ Engine  │ │ Engine  │ │ Engine  │ │ Engine      │   │
│  └─────────┘ └─────────┘ └─────────┘ └─────────────┘   │
├─────────────────────────────────────────────────────────┤
│                  事务与并发控制层                      │
│  ┌─────────────────────────────────────────────────────┐│
│  │ 2PC分布式事务 │ 死锁检测 │ 优先级调度 │ 超时处理    ││
│  └─────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────┤
│                    持久化层                            │
│  ┌─────────────────────────────────────────────────────┐│
│  │ WAL日志 │ 快照备份 │ 故障恢复 │ 数据压缩           ││
│  └─────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────┤
│                    网络层                              │
│  ┌─────────────────────────────────────────────────────┐│
│  │ TCP/UDP │ Epoll │ 连接池 │ 负载均衡               ││
│  └─────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────┘
```

## 🚀 快速开始

### 环境要求
- **操作系统**: Linux (推荐Ubuntu 18.04+, CentOS 7+)
- **编译器**: GCC 7.0+ 或 Clang 6.0+
- **依赖**: pthread, libc
- **内存**: 最小512MB，推荐2GB+
- **存储**: 至少1GB可用空间

### 编译安装

```bash
# 克隆项目
git clone https://github.com/your-org/ConcordKV.git
cd ConcordKV

# 编译项目
cd kvserver
make all

# 运行测试
make test_all

# 安装到系统 (可选)
sudo make install
```

### 基础使用示例

```c
#include "kv_engine_interface.h"
#include "kv_engine_factory.h"

int main() {
    // 创建B+Tree存储引擎实例
    kv_engine_t *engine = kv_engine_factory_create(KV_ENGINE_BTREE);
    
    // 初始化引擎
    if (engine->init(engine, NULL) != 0) {
        fprintf(stderr, "引擎初始化失败\n");
        return -1;
    }
    
    // 存储数据
    engine->set(engine, "user:1001", "张三");
    engine->set(engine, "user:1002", "李四");
    
    // 检索数据
    char *value = engine->get(engine, "user:1001");
    printf("用户信息: %s\n", value);
    free(value);
    
    // 统计信息
    size_t count = engine->count(engine);
    printf("总记录数: %zu\n", count);
    
    // 清理资源
    engine->destroy(engine);
    kv_engine_factory_destroy(engine);
    
    return 0;
}
```

### 高级功能示例

```c
// 事务示例
kv_transaction_t *tx = kv_transaction_begin(engine);
kv_transaction_set(tx, "account:001", "1000");
kv_transaction_set(tx, "account:002", "2000");
if (kv_transaction_commit(tx) == 0) {
    printf("事务提交成功\n");
}

// 批量操作示例
const char *keys[] = {"key1", "key2", "key3"};
const char *values[] = {"val1", "val2", "val3"};
engine->batch_set(engine, keys, values, 3);
```

## 🧪 测试套件

项目提供完整的测试体系，按存储引擎分类组织：

```bash
# 运行所有测试
make test_by_category

# 分类测试
make run_btree_test        # B+Tree存储引擎测试
make run_production_test   # B+Tree生产级检查
make test_hash            # Hash存储引擎测试  
make test_rbtree          # RBTree存储引擎测试
make test_array           # Array存储引擎测试
make test_system          # 系统功能测试

# 性能测试
make perf_test

# 内存检查
make memcheck
```

### 测试目录结构
```
tests/kvserver_tests/
├── btree_tests/          # B+Tree存储引擎测试
├── hash_tests/           # Hash存储引擎测试
├── rbtree_tests/         # RBTree存储引擎测试  
├── array_tests/          # Array存储引擎测试
├── system_tests/         # 系统功能测试
└── engine_tests/         # 引擎接口测试
```

## 📚 文档

- [开发路线图](docs/ROADMAP.md) - 项目发展计划和进度
- [API文档](docs/API.md) - 详细的API使用说明
- [架构设计](docs/ARCHITECTURE.md) - 系统架构和设计理念
- [配置指南](docs/CONFIG.md) - 配置参数说明
- [部署指南](docs/DEPLOYMENT.md) - 生产环境部署指南
- [贡献指南](docs/CONTRIBUTING.md) - 如何参与项目开发

## 🎯 路线图亮点

### ✅ 已完成 (Phase 2.1 - B+Tree存储引擎)
- **核心B+Tree实现** - 完整的B+Tree数据结构
- **适配器层集成** - 与统一接口无缝集成  
- **生产级测试** - 100%测试通过，832K ops/sec性能
- **并发安全性** - 8线程稳定并发测试
- **内存高效性** - 仅4-5%内存开销

### 🚧 开发中 (Phase 3 - LSM-Tree存储引擎)
- **MemTable实现** - 内存表和不可变内存表
- **SSTable文件格式** - 高效的磁盘存储格式
- **压缩机制** - Level-0和Level-N压缩策略
- **写入优化** - 面向写密集型场景的优化

### 🔮 规划中
- **分布式扩展** - 水平扩展和数据分片
- **SQL接口** - 类SQL查询语言支持
- **机器学习集成** - 智能缓存和负载预测

## 📊 基准测试

### 环境配置
- **CPU**: Intel Xeon E5-2686 v4 @ 2.3GHz
- **内存**: 16GB DDR4
- **存储**: SSD 500GB
- **网络**: 1Gbps Ethernet

### 性能对比 (单节点)

| 存储引擎 | 写入 (ops/sec) | 读取 (ops/sec) | 内存开销 | 并发安全 |
|----------|----------------|----------------|----------|----------|
| B+Tree ⭐ | 832,484        | 1,100,000+     | 4-5%     | ✅ 良好  |
| Hash     | 1,460,000      | 1,800,000      | 15-20%   | ✅ 良好  |
| RBTree   | 450,000        | 650,000        | 8-12%    | ✅ 良好  |
| Array    | 2,000,000      | 2,500,000      | 0%       | ⚠️ 基础   |

## 🤝 贡献

我们欢迎各种形式的贡献！

### 贡献方式
- 🐛 **Bug报告** - 通过Issue报告问题
- 💡 **功能建议** - 提出新功能想法
- 📝 **文档改进** - 完善文档和示例
- 🔧 **代码贡献** - 提交Pull Request

### 开发流程
1. Fork项目并创建功能分支
2. 确保代码符合项目规范
3. 添加测试用例和文档
4. 通过所有CI检查
5. 提交Pull Request

## 📄 许可证

本项目采用[MIT License](LICENSE)开源协议。

## 🙏 致谢

感谢所有为ConcordKV项目做出贡献的开发者和社区成员！

## 📞 联系我们

- **项目主页**: https://github.com/your-org/ConcordKV
- **问题反馈**: https://github.com/your-org/ConcordKV/issues
- **技术讨论**: https://discord.gg/concordkv

---

⭐ **如果您觉得ConcordKV有用，请给我们一个Star！**

