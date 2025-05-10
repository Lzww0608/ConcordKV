# ConcordKV Raft服务器

这是ConcordKV分布式键值存储系统的Raft共识实现部分。本模块使用Go语言实现，负责节点之间的通信、一致性保证和集群管理。

## 架构

Raft服务器组件包括：

- **共识模块**：实现Raft算法核心，包括领导者选举、日志复制和成员变更
- **存储模块**：管理Raft日志和状态持久化
- **通信模块**：处理节点间的RPC通信
- **状态机**：将共识日志应用到键值存储引擎
- **客户端API**：提供外部访问接口

## 目录结构

```
raftserver/
├── cmd/              - 命令行工具
├── config/           - 配置文件和管理
├── raft/             - Raft算法核心实现
│   ├── log/          - 日志管理
│   ├── node/         - 节点实现
│   ├── state/        - 状态管理
│   └── transport/    - 网络传输
├── server/           - 服务器实现
├── storage/          - 存储接口与实现
├── api/              - API定义
└── util/             - 工具函数
```

## 开发状态

目前该模块处于开发初期阶段，计划实现以下功能：

- [x] 项目结构设计
- [ ] 配置管理
- [ ] Raft核心算法
  - [ ] 领导者选举
  - [ ] 日志复制
  - [ ] 成员变更
- [ ] 存储接口
- [ ] 与C/C++存储引擎的集成
- [ ] 客户端API
- [ ] 性能优化

## 构建与运行

### 环境要求

- Go 1.16+

### 构建

```bash
go build -o concord_raft ./cmd/server
```

### 运行

```bash
./concord_raft --config=config/server.yaml
```

## 测试

```bash
go test ./...
```

## 文档

详细的设计文档和API参考请参见 [docs/raft-design.md](../docs/raft-design.md) 