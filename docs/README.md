# ConcordKV 文档中心

欢迎来到 ConcordKV 文档中心！这里提供了项目的完整文档，帮助您快速了解和使用 ConcordKV 分布式键值存储系统。

## 📚 文档导航

### 🚀 快速开始
- [项目概述](../README.md) - 项目介绍、功能特性和快速开始指南
- [系统架构](../ARCHITECTURE.md) - 详细的系统架构设计和技术细节

### 📖 核心文档
- [开发计划](ROADMAP.md) - 项目开发路线图和进展状态
- [API参考](kvserver-api-reference.md) - 完整的存储引擎API文档
- [测试指南](kvserver-testing-guide.md) - 全面的测试方法和最佳实践
- [Raft设计](raft-design.md) - Raft共识协议的设计和实现

### 🧪 测试相关
- [测试套件概览](../tests/README.md) - 测试目录结构和使用指南
- [增强持久化完成总结](../tests/COMPLETION_SUMMARY.md) - 最新功能开发完成报告
- [Raft服务器测试结果](../tests/raftserver/TEST_RESULTS.md) - Raft服务器测试验证结果

### 🔧 模块文档

#### 存储引擎 (kvserver)
- [API参考](kvserver-api-reference.md) - 存储引擎完整API文档
- [测试指南](kvserver-testing-guide.md) - 存储引擎测试方法

#### Raft服务器 (raftserver)
- [Raft服务器README](../raftserver/README.md) - Raft服务器使用指南
- [Raft设计文档](raft-design.md) - Raft协议设计细节

#### 客户端库 (client)
- [Go客户端](../client/go/README.md) - Go语言客户端使用指南
- [客户端概览](../client/README.md) - 客户端库总体介绍

#### 通用模块 (common)
- [Common模块](../common/README.md) - 通用工具和基础设施
- [API参考](../common/doc/api_reference.md) - Common模块API文档
- [架构设计](../common/doc/architecture.md) - Common模块架构设计
- [使用示例](../common/doc/examples.md) - 详细的使用示例和最佳实践

## 📋 文档分类

### 按用户类型

#### 👨‍💻 开发者
- [项目概述](../README.md) - 了解项目基本信息
- [系统架构](../ARCHITECTURE.md) - 理解系统设计
- [API参考](kvserver-api-reference.md) - 查阅API接口
- [测试指南](kvserver-testing-guide.md) - 学习测试方法

#### 🔧 运维人员
- [快速开始](../README.md#快速开始) - 部署和运行指南
- [测试套件](../tests/README.md) - 验证系统功能
- [监控指标](../common/doc/examples.md#监控指标示例) - 系统监控方法

#### 📊 架构师
- [系统架构](../ARCHITECTURE.md) - 深入理解架构设计
- [Raft设计](raft-design.md) - 分布式一致性设计
- [性能特性](../README.md#性能特性) - 了解性能指标

### 按功能模块

#### 存储引擎
- [API文档](kvserver-api-reference.md)
- [测试指南](kvserver-testing-guide.md)
- [增强持久化](../tests/COMPLETION_SUMMARY.md)

#### 分布式协调
- [Raft设计](raft-design.md)
- [Raft服务器](../raftserver/README.md)
- [测试结果](../tests/raftserver/TEST_RESULTS.md)

#### 客户端接口
- [Go客户端](../client/go/README.md)
- [客户端概览](../client/README.md)

#### 基础设施
- [Common模块](../common/README.md)
- [工具和配置](../common/doc/api_reference.md)

## 🔍 快速查找

### 常见问题
- **如何开始使用？** → [快速开始](../README.md#快速开始)
- **如何运行测试？** → [测试指南](../tests/README.md#快速开始)
- **API如何调用？** → [API参考](kvserver-api-reference.md)
- **性能如何？** → [性能特性](../README.md#性能特性)
- **架构是什么样的？** → [系统架构](../ARCHITECTURE.md)

### 开发任务
- **添加新功能** → [开发计划](ROADMAP.md) + [API参考](kvserver-api-reference.md)
- **修复Bug** → [测试指南](kvserver-testing-guide.md)
- **性能优化** → [架构文档](../ARCHITECTURE.md#性能优化策略)
- **集成测试** → [测试套件](../tests/README.md)

### 部署运维
- **单节点部署** → [快速开始](../README.md#启动服务)
- **集群部署** → [Raft服务器](../raftserver/README.md)
- **监控配置** → [Common模块](../common/doc/examples.md)
- **故障排除** → [测试指南](kvserver-testing-guide.md#故障排除)

## 📈 项目状态

### ✅ 已完成
- 核心存储引擎 (数组、红黑树、哈希表)
- 增强持久化机制 (日志压缩、增量持久化)
- 完整的Raft协议实现
- 事务支持和并发控制
- Go客户端库
- 完整的测试套件

### 🚧 进行中
- 死锁检测和预防
- 分布式事务支持
- C++客户端库
- 性能优化

### 🔮 计划中
- 跨数据中心复制
- 可视化管理界面
- 更多编程语言客户端
- 安全性增强

## 🤝 贡献文档

我们欢迎文档贡献！如果您发现文档有误或需要改进：

1. **报告问题**：在GitHub Issues中报告文档问题
2. **提交改进**：直接提交Pull Request改进文档
3. **添加示例**：贡献更多使用示例和最佳实践
4. **翻译文档**：帮助翻译文档到其他语言

### 文档规范
- 使用清晰的标题和结构
- 提供完整的代码示例
- 包含必要的解释和背景信息
- 保持文档的及时更新

---

**ConcordKV** - 高性能分布式键值存储系统

如有疑问，请查阅相关文档或在GitHub Issues中提问。 