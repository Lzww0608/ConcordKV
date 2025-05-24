# ConcordKV Raft服务器

这是ConcordKV分布式键值存储系统的Raft共识实现部分。本模块使用Go语言实现，负责节点之间的通信、一致性保证和集群管理。

## 项目结构

```
raftserver/
├── cmd/                 - 命令行工具
│   ├── server/         - 主服务器程序
│   └── test/           - 测试客户端
├── config/             - 配置文件和管理
├── raft/               - Raft算法核心实现
│   ├── types.go        - 核心类型定义
│   ├── node.go         - Raft节点实现
│   ├── election.go     - 选举和心跳逻辑
│   └── rpc.go          - RPC处理器
├── server/             - 服务器实现
├── storage/            - 存储接口与实现
│   └── memory.go       - 内存存储实现
├── transport/          - 网络传输层
│   └── http.go         - HTTP传输实现
├── statemachine/       - 状态机
│   └── kv.go           - 键值存储状态机
└── README.md
```

## 已完成功能

### Raft 核心算法
- ✅ 领导者选举
- ✅ 日志复制
- ✅ 安全性保证
- ✅ 快照机制
- ✅ 集群成员管理

### 存储层
- ✅ 内存存储实现
- ✅ 日志持久化
- ✅ 快照存储
- ✅ 状态恢复

### 网络传输
- ✅ HTTP传输层
- ✅ RPC通信
- ✅ 错误处理
- ✅ 超时机制

### 状态机
- ✅ 键值存储实现
- ✅ 命令应用
- ✅ 快照创建和恢复

### API接口
- ✅ RESTful API
- ✅ 客户端操作接口
- ✅ 管理和监控接口

## 构建与运行

### 环境要求

- Go 1.19+

### 构建

```bash
# 构建服务器
cd cmd/server
go build -o concord_raft

# 构建测试客户端
cd ../test
go build -o test_client
```

### 单节点运行

```bash
# 使用命令行参数运行
./concord_raft -node node1 -listen :8080 -api :8081

# 或使用配置文件
./concord_raft -config ../../config/example.yaml
```

### 集群运行

创建三个配置文件：

**node1.yaml:**
```yaml
server:
  nodeId: "node1"
  listenAddr: ":8080"
  apiAddr: ":8081"
  peers:
    - "node1:localhost:8080"
    - "node2:localhost:8082"
    - "node3:localhost:8084"
```

**node2.yaml:**
```yaml
server:
  nodeId: "node2"
  listenAddr: ":8082"
  apiAddr: ":8083"
  peers:
    - "node1:localhost:8080"
    - "node2:localhost:8082"
    - "node3:localhost:8084"
```

**node3.yaml:**
```yaml
server:
  nodeId: "node3"
  listenAddr: ":8084"
  apiAddr: ":8085"
  peers:
    - "node1:localhost:8080"
    - "node2:localhost:8082"
    - "node3:localhost:8084"
```

启动集群：
```bash
# 终端1
./concord_raft -config node1.yaml

# 终端2
./concord_raft -config node2.yaml

# 终端3
./concord_raft -config node3.yaml
```

## API 使用

### 键值操作

```bash
# 设置键值
curl -X POST http://localhost:8081/api/set \
  -H "Content-Type: application/json" \
  -d '{"key": "name", "value": "ConcordKV"}'

# 获取键值
curl "http://localhost:8081/api/get?key=name"

# 删除键值
curl -X DELETE "http://localhost:8081/api/delete?key=name"

# 获取所有键
curl "http://localhost:8081/api/keys"
```

### 管理接口

```bash
# 查看节点状态
curl "http://localhost:8081/api/status"

# 查看详细指标
curl "http://localhost:8081/api/metrics"

# 查看日志（调试用）
curl "http://localhost:8081/api/logs"
```

## 测试

运行测试客户端：

```bash
# 启动服务器后运行测试
./test_client
```

## 架构特点

### Raft 实现
- 严格按照Raft论文实现
- 支持动态配置变更
- 实现日志压缩优化
- 提供完整的错误处理

### 模块化设计
- 清晰的接口定义
- 可插拔的组件
- 易于扩展和测试

### 生产就绪特性
- 完整的日志记录
- 性能监控指标
- 优雅关闭机制
- 错误恢复能力

## 开发状态

当前实现提供了完整的Raft一致性协议框架，包括：

- [x] Raft核心算法实现
- [x] 网络传输层
- [x] 存储抽象层
- [x] 状态机接口
- [x] HTTP API服务器
- [x] 配置管理
- [x] 命令行工具
- [x] 基础测试

### 后续优化方向

1. **性能优化**
   - 批量日志写入
   - 并发日志复制
   - 网络连接池

2. **持久化存储**
   - 文件存储后端
   - WAL日志优化
   - 压缩和清理

3. **集群管理**
   - 自动发现机制
   - 配置变更API
   - 故障检测

4. **监控和运维**
   - Prometheus指标
   - 分布式跟踪
   - 健康检查

## 贡献

欢迎提交Issue和Pull Request来改进这个实现。请确保：

1. 代码符合Go编码规范
2. 添加必要的测试
3. 更新相关文档

## 许可证

本项目采用 MIT 许可证。 