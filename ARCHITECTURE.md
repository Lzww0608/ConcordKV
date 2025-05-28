# ConcordKV 系统架构文档

## 1. 架构概览

ConcordKV 采用分层模块化架构，将分布式键值存储系统分解为多个独立但协作的组件。系统设计遵循高内聚、低耦合的原则，确保各模块可以独立开发、测试和部署。

```
┌─────────────────────────────────────────────────────────┐
│                    Client Layer                        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │   Go Client │  │  C++ Client │  │ REST Client │     │
│  └─────────────┘  └─────────────┘  └─────────────┘     │
└─────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────┐
│                 Distributed Layer (Go)                 │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              Raft Consensus Engine                 │ │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │ │
│  │  │   Leader    │ │  Follower   │ │  Candidate  │   │ │
│  │  │  Election   │ │    Log      │ │   Election  │   │ │
│  │  └─────────────┘ └─────────────┘ └─────────────┘   │ │
│  └─────────────────────────────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              Cluster Management                    │ │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │ │
│  │  │   Node      │ │   Shard     │ │   Health    │   │ │
│  │  │ Discovery   │ │ Management  │ │   Monitor   │   │ │
│  │  └─────────────┘ └─────────────┘ └─────────────┘   │ │
│  └─────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────┐
│                Storage Engine (C/C++)                  │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              Enhanced Persistence                  │ │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │ │
│  │  │     WAL     │ │    Log      │ │ Incremental │   │ │
│  │  │ Compaction  │ │  Rotation   │ │   Snapshot  │   │ │
│  │  └─────────────┘ └─────────────┘ └─────────────┘   │ │
│  └─────────────────────────────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              Core Storage                          │ │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │ │
│  │  │    Array    │ │   RB-Tree   │ │  Hash Table │   │ │
│  │  │   Storage   │ │   Storage   │ │   Storage   │   │ │
│  │  └─────────────┘ └─────────────┘ └─────────────┘   │ │
│  └─────────────────────────────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────┐ │
│  │            Transaction & Concurrency               │ │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │ │
│  │  │ Transaction │ │ Lock Manager│ │   Deadlock  │   │ │
│  │  │   Manager   │ │   (MVCC)    │ │  Detection  │   │ │
│  │  └─────────────┘ └─────────────┘ └─────────────┘   │ │
│  └─────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

## 2. 核心组件详细设计

### 2.1 存储引擎层 (C/C++)

存储引擎是系统的核心，负责数据的本地存储、检索和管理。

#### 2.1.1 增强持久化子系统

**WAL日志压缩**
- **触发条件**: 日志条目数量超过1000条或文件大小超过64MB
- **压缩策略**: 移除冗余操作，只保留每个键的最新状态
- **空间节省**: 通常可节省30-70%的存储空间
- **后台处理**: 独立线程执行，不阻塞正常操作

**增量持久化**
- **批量同步**: 累积100个操作后批量写入磁盘
- **定时同步**: 每5秒强制同步一次
- **性能优化**: 减少磁盘I/O操作，提高写入吞吐量
- **数据安全**: 保证最终一致性，支持崩溃恢复

**日志轮转**
- **文件管理**: 维护多个日志文件的链表结构
- **自动轮转**: 文件超过64MB时自动创建新文件
- **历史清理**: 压缩后自动清理旧文件

#### 2.1.2 存储结构实现

**数组存储**
```c
typedef struct {
    kv_item_t *items;     // 键值对数组
    int capacity;         // 容量
    int count;           // 当前数量
    pthread_rwlock_t lock; // 读写锁
} array_t;
```
- **适用场景**: 小规模数据集，顺序访问
- **时间复杂度**: O(n)查找，O(1)插入
- **内存占用**: 连续内存，缓存友好

**红黑树存储**
```c
typedef struct rb_node {
    char *key;
    char *value;
    int color;           // 红黑树颜色
    struct rb_node *left, *right, *parent;
} rb_node_t;
```
- **适用场景**: 需要有序遍历，范围查询
- **时间复杂度**: O(log n)查找、插入、删除
- **平衡性**: 自平衡二叉搜索树

**哈希表存储**
```c
typedef struct {
    hash_bucket_t *buckets; // 哈希桶数组
    int bucket_count;       // 桶数量
    int total_items;        // 总项目数
    double load_factor;     // 负载因子
    pthread_mutex_t *locks; // 分段锁
} hash_table_t;
```
- **适用场景**: 随机访问，高并发读写
- **时间复杂度**: O(1)平均查找时间
- **并发优化**: 分段锁减少锁竞争

#### 2.1.3 事务和并发控制

**MVCC实现**
```c
typedef struct {
    uint64_t txn_id;        // 事务ID
    uint64_t start_ts;      // 开始时间戳
    uint64_t commit_ts;     // 提交时间戳
    uint8_t isolation_level; // 隔离级别
    txn_op_list_t *ops;     // 操作列表
} transaction_t;
```

**锁管理器**
- **读写锁**: 支持多读者单写者
- **分段锁**: 减少锁粒度，提高并发性
- **死锁检测**: 基于等待图的死锁检测算法
- **锁超时**: 防止长时间锁定

### 2.2 分布式协调层 (Go)

#### 2.2.1 Raft共识协议

**状态机设计**
```go
type RaftNode struct {
    state        NodeState    // FOLLOWER, CANDIDATE, LEADER
    currentTerm  uint64       // 当前任期
    votedFor     string       // 投票对象
    log          []LogEntry   // 日志条目
    commitIndex  uint64       // 已提交索引
    lastApplied  uint64       // 已应用索引
    
    // Leader状态
    nextIndex    map[string]uint64  // 下一个发送索引
    matchIndex   map[string]uint64  // 已匹配索引
}
```

**选举机制**
- **超时机制**: 随机选举超时(150-300ms)
- **投票策略**: 先到先得，日志完整性检查
- **脑裂预防**: 要求大多数节点投票

**日志复制**
- **并行复制**: 向所有Follower并行发送
- **一致性检查**: 基于日志索引和任期的一致性验证
- **重试机制**: 失败时自动重试和回退

#### 2.2.2 集群管理

**节点发现**
```go
type NodeDiscovery struct {
    discoveryType DiscoveryType  // STATIC, DNS, SERVICE_REGISTRY
    endpoints     []string       // 静态端点列表
    registry      ServiceRegistry // 服务注册中心
    refreshInterval time.Duration // 刷新间隔
}
```

**负载均衡**
- **轮询策略**: 简单轮询分配
- **加权策略**: 基于节点性能的加权分配
- **一致性哈希**: 特定键总是路由到相同节点
- **最少连接**: 选择连接数最少的节点

**健康检查**
```go
type HealthChecker struct {
    checkInterval time.Duration   // 检查间隔
    timeout       time.Duration   // 超时时间
    retryCount    int            // 重试次数
    metrics       *HealthMetrics // 健康指标
}
```

### 2.3 客户端层

#### 2.3.1 Go客户端架构

**连接管理**
```go
type ConnectionPool struct {
    connections map[string]*Connection // 连接池
    balancer    LoadBalancer          // 负载均衡器
    discovery   *NodeDiscovery        // 节点发现
    monitor     *HealthMonitor        // 健康监控
}
```

**事务支持**
```go
type Transaction struct {
    client      *Client
    txnID       string
    isolation   IsolationLevel
    operations  []Operation
    readOnly    bool
    timeout     time.Duration
}
```

**批量操作**
```go
type BatchOperation struct {
    operations []Operation
    parallel   bool          // 是否并行执行
    timeout    time.Duration // 超时时间
}
```

## 3. 数据流和交互模式

### 3.1 写操作流程

```
Client → Raft Leader → Log Replication → Majority Commit → Apply to State Machine → Response
```

1. **客户端请求**: 客户端发送写请求到Raft Leader
2. **日志追加**: Leader将操作追加到本地日志
3. **并行复制**: Leader并行向所有Follower发送日志条目
4. **多数确认**: 等待大多数节点确认接收
5. **提交应用**: 将日志条目标记为已提交并应用到状态机
6. **响应客户端**: 返回操作结果给客户端

### 3.2 读操作流程

```
Client → Load Balancer → Any Node → Local Read → Response
```

1. **负载均衡**: 客户端通过负载均衡器选择节点
2. **本地读取**: 从选中节点的本地状态机读取数据
3. **一致性保证**: 可选的线性一致性读取验证
4. **返回结果**: 直接返回读取结果

### 3.3 故障恢复流程

**Leader故障**
```
Follower Timeout → Election → New Leader → Resume Operations
```

**Follower故障**
```
Leader Detect → Remove from Replication → Continue with Majority
```

**网络分区**
```
Partition Detection → Majority Side Continues → Minority Side Blocks → Heal and Sync
```

## 4. 性能优化策略

### 4.1 存储层优化

- **内存池**: 减少内存分配开销
- **批量写入**: 聚合多个操作减少I/O
- **异步刷盘**: 后台异步持久化
- **压缩算法**: 智能日志压缩策略

### 4.2 网络层优化

- **连接复用**: 长连接和连接池
- **批量传输**: 聚合多个请求
- **压缩传输**: 网络数据压缩
- **并行处理**: 多线程处理网络I/O

### 4.3 并发优化

- **无锁数据结构**: 关键路径使用无锁算法
- **分段锁**: 减少锁粒度和竞争
- **读写分离**: 读操作不阻塞写操作
- **异步处理**: 非关键操作异步执行

## 5. 可扩展性设计

### 5.1 水平扩展

- **动态分片**: 根据负载自动调整分片
- **数据迁移**: 在线数据重平衡
- **弹性伸缩**: 自动添加/移除节点
- **跨区域复制**: 支持多数据中心部署

### 5.2 垂直扩展

- **资源隔离**: CPU、内存、磁盘资源隔离
- **优先级调度**: 不同操作的优先级管理
- **缓存层**: 多级缓存提高性能
- **存储分层**: 热数据和冷数据分离

## 6. 监控和运维

### 6.1 指标监控

- **性能指标**: QPS、延迟、吞吐量
- **资源指标**: CPU、内存、磁盘、网络
- **业务指标**: 事务成功率、数据一致性
- **错误指标**: 错误率、超时率

### 6.2 日志系统

- **结构化日志**: JSON格式的结构化日志
- **日志级别**: DEBUG、INFO、WARN、ERROR
- **日志轮转**: 自动日志文件管理
- **集中收集**: 支持日志聚合和分析

### 6.3 运维工具

- **健康检查**: 自动健康状态检测
- **故障恢复**: 自动故障检测和恢复
- **配置管理**: 动态配置更新
- **备份恢复**: 自动化备份和恢复流程

---

本架构文档描述了ConcordKV系统的核心设计理念和实现细节。系统采用模块化设计，各组件职责明确，通过标准化接口协作，确保系统的可维护性、可扩展性和高性能。 