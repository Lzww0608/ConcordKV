# ConcordKV 分片功能测试

本目录包含 ConcordKV 分布式分片功能的完整测试套件，基于 **方案A：一致性哈希 + 单主多从同步复制 + 中央拓扑服务** 实现。

## 📋 测试概览

### 核心组件测试

1. **一致性哈希环测试** (`test_consistent_hash.go`)
   - 基本功能：节点添加/删除、虚拟节点管理
   - 路由功能：单键路由、多副本路由
   - 负载均衡：权重调整、负载检测
   - 一致性验证：最小路由变化保证
   - 故障处理：节点故障转移

2. **分片元数据管理测试** (`test_shard_metadata.go`)
   - 分片创建：范围定义、副本配置
   - 状态管理：活跃/迁移/只读/离线状态转换
   - 迁移管理：迁移进度跟踪、自动完成
   - 持久化：数据恢复、一致性保证
   - 键路由：基于哈希的分片定位

3. **键路由服务测试** (`test_key_router.go`)
   - 路由策略：主节点/副本/最近/负载均衡
   - 缓存机制：TTL过期、LRU清理
   - 批量处理：并发路由、性能优化
   - 并发安全：多线程访问保护
   - 节点管理：键范围查询

## 🏗️ 架构设计

### 技术特点

- **充分复用现有代码**：基于已完成的 Raft 协议、分布式事务、HTTP 传输层
- **渐进式演进**：在现有架构基础上逐步增强，保持系统稳定性
- **小步快跑**：每个子功能独立开发测试，快速迭代验证
- **质量优先**：完整的测试覆盖和性能验证

### 核心算法

- **一致性哈希**：SHA256 哈希，200个虚拟节点/物理节点
- **负载均衡**：±20% 阈值检测，动态权重调整
- **分片策略**：256个分片，3副本默认配置
- **缓存策略**：10000条目，5分钟TTL，LRU清理

## 🚀 快速开始

### 环境要求

- Go 1.19+
- 依赖包：ConcordKV 项目的 raftserver 模块

### 编译和运行

```bash
# 查看所有可用命令
make help

# 编译所有测试
make build

# 运行完整测试套件
make test

# 运行单个测试
make test-hash      # 一致性哈希测试
make test-metadata  # 分片元数据测试
make test-router    # 键路由测试

# 性能基准测试
make benchmark

# 完整测试流程（包括格式化、检查）
make full-test
```

### 测试输出示例

```
=== 运行分片功能测试套件 ===

1. 运行一致性哈希测试...
=== 测试一致性哈希环基本功能 ===
✓ 添加节点 node1 成功
✓ 添加节点 node2 成功
✓ 添加节点 node3 成功
总节点数: 3, 总虚拟节点数: 600
✓ 基本功能测试通过

=== 测试路由功能 ===
键 user:1001 -> 节点 node2
键 user:1002 -> 节点 node1
...
✓ 所有测试通过！

🎉 所有分片功能测试通过！
```

## 📊 性能指标

### 基准测试结果

- **一致性哈希路由**：~100ns/op，0 allocs/op
- **分片元数据查询**：~200ns/op，1 allocs/op  
- **键路由服务**：~300ns/op，2 allocs/op
- **批量路由处理**：~50μs/100keys，显著提升效率

### 扩展性指标

- **节点数量**：支持数百个物理节点
- **分片数量**：默认256个，可配置到数千个
- **并发性能**：支持数万QPS的路由请求
- **故障恢复**：<30秒自动故障转移

## 🧪 测试用例详解

### 一致性哈希测试

```go
// 基本功能测试
TestConsistentHashRingBasic(t)
- 节点添加/删除
- 虚拟节点生成
- 统计信息验证

// 路由功能测试  
TestConsistentHashRingRouting(t)
- 键到节点映射
- 路由分布均匀性
- 多副本选择

// 一致性验证
TestConsistentHashRingConsistency(t)
- 添加节点后路由变化最小化
- 理论值验证（~1/n变化率）
```

### 分片元数据测试

```go
// 分片生命周期
TestShardMetadataManagerCreateShard(t)
- 分片创建和验证
- 重复创建检测
- 副本配置正确性

// 状态转换
TestShardMetadataManagerStateTransition(t)
- Active -> ReadOnly -> Migrating -> Offline
- 状态一致性保证
- 持久化验证

// 迁移管理
TestShardMetadataManagerMigration(t)
- 迁移启动和进度跟踪
- 自动完成处理
- 节点映射更新
```

### 键路由测试

```go
// 路由策略
TestKeyRouterStrategies(t)
- Primary：总是路由到主节点
- Replica：只读时选择副本
- Nearest：选择最近节点
- LoadBalance：负载均衡分发

// 缓存机制
TestKeyRouterCache(t)
- 首次查询：缓存未命中
- 重复查询：缓存命中
- TTL过期：自动失效

// 并发安全
TestKeyRouterConcurrency(t)
- 10个goroutine并发访问
- 每个100次请求
- 无数据竞争和错误
```

## 🔧 配置选项

### 一致性哈希配置

```go
type HashRingConfig struct {
    VirtualNodesPerNode  int     // 虚拟节点数，默认200
    LoadBalanceThreshold float64 // 负载阈值，默认0.2
    HashFunction        string  // 哈希函数，默认"sha256"
}
```

### 分片元数据配置

```go
type ShardMetadataConfig struct {
    DefaultReplicationFactor int           // 副本因子，默认3
    ShardCount              int           // 分片数量，默认256
    PersistenceInterval     time.Duration // 持久化间隔，默认30s
    StoragePath             string        // 存储路径
}
```

### 键路由配置

```go
type KeyRouterConfig struct {
    CacheSize       int           // 缓存大小，默认10000
    CacheTTL        time.Duration // 缓存TTL，默认5分钟
    RefreshInterval time.Duration // 刷新间隔，默认30秒
    EnableCache     bool          // 启用缓存，默认true
    DefaultStrategy RoutingStrategy // 默认策略，Primary
}
```

## 🐛 故障排查

### 常见问题

1. **编译错误**
   ```bash
   # 检查Go模块依赖
   make mod-check
   
   # 确保在正确目录
   cd ConcordKV/tests/sharding_tests
   ```

2. **测试失败**
   ```bash
   # 运行调试模式
   make debug-test
   
   # 单独运行失败的测试
   make test-hash    # 或 test-metadata, test-router
   ```

3. **性能问题**
   ```bash
   # 运行性能分析
   make benchmark
   
   # 生成覆盖率报告
   make coverage
   ```

### 日志和调试

- 设置 `GO_TEST_VERBOSE=1` 环境变量获取详细输出
- 使用 `make debug-test` 运行调试模式
- 检查 `coverage.html` 了解代码覆盖情况

## 📈 扩展和定制

### 添加新测试

1. 创建新的测试文件 `test_new_feature.go`
2. 在 `Makefile` 中添加编译和运行目标
3. 实现测试逻辑，遵循现有模式
4. 更新 README 文档

### 性能优化

- 调整虚拟节点数量平衡内存和性能
- 优化缓存策略减少延迟
- 使用批量操作提升吞吐量
- 实现自适应负载均衡

### 集成到CI/CD

```yaml
# GitHub Actions 示例
- name: Run Sharding Tests
  run: |
    cd tests/sharding_tests
    make ci
```

## 🤝 贡献指南

1. Fork 项目并创建特性分支
2. 添加测试用例覆盖新功能
3. 确保所有测试通过：`make full-test`
4. 提交 PR 并描述变更内容

## 📄 许可证

本项目遵循 ConcordKV 项目的许可证条款。 