# ConcordKV 存储引擎开发与测试指南

本文档提供了ConcordKV存储引擎的详细开发和测试指南，包括架构说明、模块介绍、API使用方法以及测试流程。

## 1. 架构概览

ConcordKV存储引擎是一个基于C/C++开发的高性能键值存储系统，设计为支持分布式环境下的数据存储和访问。该引擎作为ConcordKV项目的核心组件，提供了可靠的数据存储、事务处理和集群管理功能。

### 1.1 核心特性

- **多种存储结构**：支持数组、红黑树、哈希表等多种数据结构
- **持久化机制**：基于WAL(Write-Ahead Log)和快照实现数据持久化
- **并发控制**：提供读写锁和分段锁机制，保证并发安全
- **事务支持**：实现ACID特性，支持多种隔离级别
- **集群管理**：基于Raft协议的节点管理和分片功能
- **错误处理**：详细的错误码和日志记录机制
- **快照与恢复**：支持数据快照创建和系统恢复

### 1.2 模块构成

存储引擎由以下主要模块组成：

- **核心存储模块**：`kvstore.c/h`，`kvstore_array.c`，`kvstore_rbtree.c`，`kvstore_hash.c`
- **持久化模块**：`kv_persist.c/h`
- **并发控制模块**：`kv_concurrency.c/h`
- **事务处理模块**：`kv_transaction.c/h`
- **集群管理模块**：`kv_cluster.c/h`
- **错误处理模块**：`kv_error.c/h`
- **快照管理模块**：`kv_snapshot.c/h`

## 2. 开发环境配置

### 2.1 依赖项

开发ConcordKV存储引擎需要以下依赖：

- GCC 7.0+或兼容的C/C++编译器
- Make工具
- POSIX兼容的操作系统（Linux/Unix）
- pthread库（用于并发控制）

### 2.2 编译系统

存储引擎使用Makefile进行构建。主要的编译目标包括：

- `make`：编译所有模块并生成共享库
- `make clean`：清理编译产物
- `make test`：编译并运行测试用例

### 2.3 目录结构

```
kvserver/
├── Makefile                 # 编译脚本
├── kvstore.c/h              # 核心存储接口
├── kvstore_array.c          # 数组实现
├── kvstore_rbtree.c         # 红黑树实现
├── kvstore_hash.c           # 哈希表实现
├── kv_persist.c/h           # 持久化机制
├── kv_concurrency.c/h       # 并发控制
├── kv_transaction.c/h       # 事务支持
├── kv_cluster.c/h           # 集群管理
├── kv_error.c/h             # 错误处理
├── kv_snapshot.c/h          # 快照管理
└── testcase.c               # 测试用例
```

## 3. 模块详解

### 3.1 核心存储模块

核心存储模块提供了基本的键值存储功能，支持多种数据结构实现。

#### 3.1.1 通用接口

所有存储实现都支持以下通用操作：

- `create`：创建存储实例
- `destroy`：销毁存储实例
- `set`：设置键值对
- `get`：获取键对应的值
- `delete`：删除键值对
- `modify`：修改已存在的键值对
- `count`：获取键值对数量

#### 3.1.2 存储实现

- **数组实现**：适用于小规模数据集，提供O(n)的查询性能
- **红黑树实现**：提供O(log n)的查询性能，适合需要有序遍历的场景
- **哈希表实现**：提供O(1)的平均查询性能，适合随机访问场景

### 3.2 持久化模块

持久化模块负责确保数据不会因系统崩溃而丢失。

#### 3.2.1 WAL日志

WAL日志记录所有修改操作，提供以下功能：

- `wal_init`：初始化WAL系统
- `wal_destroy`：关闭WAL系统
- `wal_append`：添加新的日志条目
- `wal_recover`：从日志恢复数据

#### 3.2.2 快照机制

快照功能允许创建数据库状态的完整备份：

- `snapshot_init`：初始化快照系统
- `snapshot_destroy`：清理快照资源
- `snapshot_create`：创建数据快照
- `snapshot_load`：加载数据快照

### 3.3 并发控制模块

并发控制模块通过锁机制保证多线程环境下的数据一致性。

#### 3.3.1 锁类型

- **读写锁**：允许多读单写
- **分段锁**：减少锁竞争，提高并发性能

#### 3.3.2 主要API

- `lock_init`：初始化锁系统
- `read_lock`/`read_unlock`：获取/释放读锁
- `write_lock`/`write_unlock`：获取/释放写锁
- `lock_destroy`：清理锁资源

### 3.4 事务处理模块

事务模块提供ACID特性支持，确保操作的原子性、一致性、隔离性和持久性。

#### 3.4.1 事务隔离级别

- 读未提交（READ UNCOMMITTED）
- 读已提交（READ COMMITTED）
- 可重复读（REPEATABLE READ）
- 串行化（SERIALIZABLE）

#### 3.4.2 事务操作

- `txn_begin`：开始新事务
- `txn_set`/`txn_get`/`txn_delete`/`txn_modify`：事务内的数据操作
- `txn_commit`：提交事务
- `txn_rollback`：回滚事务

### 3.5 集群管理模块

集群模块基于Raft协议，处理分布式环境下的节点管理和数据分片。

#### 3.5.1 节点管理

- 领导者选举
- 心跳机制
- 节点状态同步

#### 3.5.2 数据分片

- 基于键的分片路由
- 分片复制
- 数据迁移

### 3.6 错误处理模块

提供统一的错误码定义和处理机制。

- 错误码分类
- 错误日志记录
- 错误恢复策略

## 4. API使用指南

### 4.1 基本用法

```c
// 初始化存储引擎（使用红黑树实现）
rbtree_t tree;
kv_store_rbtree_create(&tree);

// 设置键值对
kvs_rbtree_set(&tree, "name", "ConcordKV");

// 获取值
char *value = kvs_rbtree_get(&tree, "name");

// 修改值
kvs_rbtree_modify(&tree, "name", "ConcordKV-v2");

// 删除键值对
kvs_rbtree_delete(&tree, "name");

// 清理资源
kv_store_rbtree_destroy(&tree);
```

### 4.2 持久化示例

```c
// 初始化持久化状态
persist_state_t ps;
persist_init(&ps, "/data/concordkv", 1); // 启用同步写入

// 添加WAL日志
wal_append(&ps.wal, WAL_OP_SET, "key1", "value1");

// 创建快照
snapshot_create(&ps.snap, &engine, save_engine_data);

// 恢复
snapshot_load(&ps.snap, &engine, load_engine_data);
wal_recover(&ps.wal, &engine, apply_log_entry);

// 清理
persist_destroy(&ps);
```

### 4.3 事务示例

```c
// 初始化事务管理器
txn_manager_t manager;
txn_manager_init(&manager);

// 开始事务
kv_transaction_t *txn = txn_begin(&manager, &engine, TXN_ISOLATION_REPEATABLE_READ);

// 执行操作
txn_set(txn, "user", "alice");
txn_set(txn, "balance", "100");

// 提交事务
txn_commit(txn);

// 或回滚事务
// txn_rollback(txn);

// 释放资源
txn_free(txn);
txn_manager_destroy(&manager);
```

### 4.4 集群操作示例

```c
// 初始化集群
cluster_t cluster;
cluster_init(&cluster, "cluster-1", CLUSTER_ROLE_MASTER, 
             "node-1", "192.168.1.1", 5000, &engine);

// 添加节点
cluster_add_node(&cluster, "node-2", "192.168.1.2", 5000, NODE_ROLE_FOLLOWER);

// 添加分片
cluster_add_shard(&cluster, 1, 0); // 分片1，主节点是索引0的节点
cluster_add_shard_replica(&cluster, 1, 1); // 分片1添加索引1的节点作为副本

// 通过键找到对应分片
int shard_id = cluster_get_shard_by_key(&cluster, "user-123");

// 集群健康检查
cluster_health_check(&cluster);

// 清理
cluster_destroy(&cluster);
```

## 5. 测试指南

### 5.1 单元测试

单元测试用例位于`testcase.c`文件中，覆盖了各个模块的基本功能。

运行单元测试：

```bash
make test
```

### 5.2 性能测试

性能测试评估存储引擎在不同工作负载下的性能表现。

#### 5.2.1 测试项目

- **吞吐量测试**：测量每秒可处理的操作数
- **延迟测试**：测量操作的响应时间
- **并发测试**：测试多线程环境下的性能
- **持久化测试**：测试WAL和快照对性能的影响

#### 5.2.2 运行性能测试

```bash
cd tests
./benchmark --duration=60 --threads=4 --value-size=1024 --write-ratio=0.3
```

### 5.3 压力测试

压力测试验证系统在极端条件下的稳定性。

#### 5.3.1 测试场景

- 大量并发连接
- 大体积数据操作
- 长时间持续运行
- 随机故障注入

#### 5.3.2 运行压力测试

```bash
cd tests
./stress-test --hours=24 --crash-probability=0.01 --network-instability=true
```

### 5.4 集成测试

集成测试验证存储引擎与其他组件（如Raft服务器）的协同工作能力。

```bash
cd tests
./integration-test --nodes=3 --duration=30m
```

## 6. 最佳实践

### 6.1 性能优化建议

- 根据工作负载选择合适的存储结构（小数据集用数组，有序访问用红黑树，随机访问用哈希表）
- 调整WAL同步策略，在性能和持久性之间取得平衡
- 使用合适的并发控制级别，避免过度锁定
- 对热点数据进行分片，避免单点瓶颈

### 6.2 安全使用指南

- 定期创建数据快照
- 监控WAL文件大小，避免磁盘空间耗尽
- 实现适当的访问控制
- 定期验证数据一致性

### 6.3 故障恢复流程

1. 加载最新的快照
2. 重放WAL日志中的未持久化操作
3. 验证系统状态并恢复服务
4. 通知集群其他节点已恢复

## 7. 故障排除

### 7.1 常见问题

- **内存泄漏**：使用Valgrind工具检测
- **死锁**：检查锁获取顺序，实现锁超时机制
- **数据不一致**：验证WAL日志完整性，检查事务隔离级别
- **性能下降**：监控磁盘I/O，检查锁竞争，优化数据结构

### 7.2 日志分析

系统日志提供重要的故障诊断信息：

- 错误日志格式解析
- 常见错误码含义
- 使用日志进行故障追踪

## 8. 扩展与定制

### 8.1 添加新的存储结构

1. 创建新的源文件（如`kvstore_btree.c`）
2. 实现标准接口（create, destroy, set, get, delete, modify, count）
3. 在`kv_store.h`中添加相应的宏定义和函数声明
4. 在Makefile中添加新文件的编译规则

### 8.2 自定义序列化格式

1. 修改`kv_persist.c`中的日志格式
2. 更新序列化和反序列化函数
3. 确保向后兼容性

### 8.3 集成外部组件

- 与监控系统集成
- 添加新的客户端协议支持
- 与其他数据库系统的数据同步

## 9. 开发路线图

参考项目的[ROADMAP.md](ROADMAP.md)文档了解未来开发计划。

## 10. 附录

### 10.1 错误码参考

详细的错误码列表和含义，请参考`kv_error.h`文件。

### 10.2 配置参数

存储引擎支持的配置参数及其默认值、有效范围和影响。

### 10.3 性能基准

在不同硬件配置下的性能测试结果和对比数据。 