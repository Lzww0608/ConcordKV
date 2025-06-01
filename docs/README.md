# ConcordKV 文档中心

欢迎来到ConcordKV项目的文档中心！本目录包含了项目的完整技术文档，按照功能模块分类组织。

## 📁 文档目录结构

### 🔧 [kvserver/](./kvserver/) - KV服务器核心组件
KV服务器的核心实现、API接口和测试指南。

- **[API.md](./kvserver/API.md)** - KV服务器API参考手册
- **[kvserver-api-reference.md](./kvserver/kvserver-api-reference.md)** - 详细的API接口文档
- **[kvserver-testing-guide.md](./kvserver/kvserver-testing-guide.md)** - 测试框架和测试指南

### 🗄️ [storage-engines/](./storage-engines/) - 存储引擎实现
各种存储引擎的设计文档、优化记录和性能分析。

- **[HASH_OPTIMIZATION_COMPLETE.md](./storage-engines/HASH_OPTIMIZATION_COMPLETE.md)** - Hash存储引擎优化完整记录
- **LSM_TREE_DESIGN.md** - LSM-Tree存储引擎设计文档 *(待创建)*
- **SSTABLE_FORMAT_SPEC.md** - SSTable文件格式规范 *(待创建)*
- **BTREE_IMPLEMENTATION.md** - B+Tree实现文档 *(待创建)*

### 🌐 [distributed-systems/](./distributed-systems/) - 分布式系统组件
分布式事务、Raft协议、集群成员管理等分布式系统核心组件。

- **[DISTRIBUTED_TRANSACTION_IMPLEMENTATION.md](./distributed-systems/DISTRIBUTED_TRANSACTION_IMPLEMENTATION.md)** - 分布式事务系统实现详解
- **[MEMBERSHIP_IMPLEMENTATION.md](./distributed-systems/MEMBERSHIP_IMPLEMENTATION.md)** - 集群成员管理实现
- **[raft-design.md](./distributed-systems/raft-design.md)** - Raft协议设计文档

### 📋 [project-management/](./project-management/) - 项目管理文档
项目路线图、开发规范、贡献指南等项目管理相关文档。

- **[ROADMAP.md](./project-management/ROADMAP.md)** - 项目开发路线图和进展跟踪
- **[IMPLEMENTATION_SUMMARY.md](./project-management/IMPLEMENTATION_SUMMARY.md)** - 项目实现总结
- **[CONTRIBUTING.md](./project-management/CONTRIBUTING.md)** - 贡献者指南

### 🐛 [debugging-guides/](./debugging-guides/) - 调试指南和问题解决
详细的调试过程记录、问题分析和解决方案。

- **[LSM_SSTABLE_DEBUG_PROCESS.md](./debugging-guides/LSM_SSTABLE_DEBUG_PROCESS.md)** - LSM-Tree SSTable实现调试过程详细记录

## 🚀 快速开始

### 对于新开发者
1. 先阅读 [ROADMAP.md](./project-management/ROADMAP.md) 了解项目整体架构和进展
2. 查看 [CONTRIBUTING.md](./project-management/CONTRIBUTING.md) 了解如何参与项目
3. 根据兴趣选择相应的技术文档深入学习

### 对于API使用者
1. 阅读 [API.md](./kvserver/API.md) 获取API概览
2. 参考 [kvserver-api-reference.md](./kvserver/kvserver-api-reference.md) 获取详细接口说明
3. 查看 [kvserver-testing-guide.md](./kvserver/kvserver-testing-guide.md) 了解测试方法

### 对于存储引擎开发者
1. 查看 [storage-engines/](./storage-engines/) 目录下的相关设计文档
2. 参考 [debugging-guides/](./debugging-guides/) 中的调试经验
3. 了解各存储引擎的性能特征和适用场景

### 对于分布式系统开发者
1. 阅读 [distributed-systems/](./distributed-systems/) 目录下的系统设计文档
2. 重点关注分布式事务和Raft协议的实现细节
3. 了解集群管理和容错机制

## 📊 项目当前状态

### ✅ 已完成模块
- **基础存储引擎**: Array、RBTree、Hash、B+Tree
- **LSM-Tree组件**: MemTable、MemTable Manager、SSTable文件格式
- **分布式事务**: 两阶段提交协议、优先级调度
- **集群管理**: Raft协议基础、成员管理

### 🔄 进行中模块
- **LSM-Tree压缩**: Compaction机制和Level管理
- **完整集成**: LSM-Tree主控制器、WAL集成
- **性能优化**: 多存储引擎优化和监控

### 📈 性能指标
| 存储引擎 | 插入性能 | 查找性能 | 内存开销 | 状态 |
|----------|----------|----------|----------|------|
| LSM Manager | 639K ops/sec | 2.17M ops/sec | 137B/entry | 🟢 生产就绪 |
| SSTable | 1.66M ops/sec | 高效 | 105.7B/entry | 🟢 生产就绪 |
| B+Tree | 105K ops/sec | 105K ops/sec | 4-5% | 🟢 生产就绪 |
| Hash | 1.46M ops/sec | 高效 | 15-20% | 🟢 生产就绪 |

## 📝 文档贡献

### 文档规范
- 使用 Markdown 格式
- 包含详细的代码示例和性能数据
- 提供问题分析和解决方案
- 保持文档的时效性和准确性

### 新增文档
如需新增文档，请按照以下分类放置：
- **技术实现**: 存储引擎或分布式系统实现 → `storage-engines/` 或 `distributed-systems/`
- **API文档**: 接口说明和使用指南 → `kvserver/`
- **调试记录**: 问题分析和解决过程 → `debugging-guides/`
- **项目管理**: 计划、流程、规范 → `project-management/`

### 文档更新
定期更新文档内容，确保与代码实现同步：
- 性能数据更新
- 新功能文档补充
- 已知问题和解决方案更新

## 🔗 相关链接

- **项目仓库**: [ConcordKV GitHub](https://github.com/username/ConcordKV)
- **问题跟踪**: [GitHub Issues](https://github.com/username/ConcordKV/issues)
- **讨论区**: [GitHub Discussions](https://github.com/username/ConcordKV/discussions)
- **发布记录**: [Releases](https://github.com/username/ConcordKV/releases)

## 🎯 文档质量目标

- **完整性**: 覆盖所有主要功能模块
- **准确性**: 与代码实现保持同步
- **可读性**: 清晰的结构和详细的说明
- **实用性**: 包含实际的使用示例和性能数据

---

**最后更新**: 2025-6-1  
**项目版本**: v0.3.0  
**文档状态**: 🟢 活跃维护  

有任何文档相关问题，请提交 Issue 或 Pull Request！ 