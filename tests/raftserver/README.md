# ConcordKV Raft服务器测试套件

本目录包含ConcordKV Raft服务器的完整测试套件，用于验证Raft一致性协议实现、API服务器和键值存储功能。

## 测试文件说明

### 测试脚本

- **`test.sh`** - 基础测试脚本，验证基本功能
- **`test_improved.sh`** - 改进的测试脚本，包含更详细的测试用例和错误处理
- **`test_complete.sh`** - 完整的测试脚本，包含所有API端点的测试
- **`debug.sh`** - 调试脚本，用于快速启动服务器进行调试
- **`run_test_api.sh`** - 运行Go测试程序的辅助脚本

### 测试程序

- **`test_api.go`** - Go语言编写的API测试程序，用于编程方式测试服务器功能

### 测试文档

- **`TEST_RESULTS.md`** - 详细的测试结果报告，包含问题解决过程和性能数据

## 使用方法

### 运行基础测试
```bash
cd /path/to/ConcordKV/raftserver
chmod +x ../tests/raftserver/test.sh
../tests/raftserver/test.sh
```

### 运行完整测试
```bash
cd /path/to/ConcordKV/raftserver
chmod +x ../tests/raftserver/test_complete.sh
../tests/raftserver/test_complete.sh
```

### 运行Go测试程序
```bash
# 方法1：使用运行脚本（推荐）
chmod +x run_test_api.sh
./run_test_api.sh

# 方法2：手动切换目录
cd ../../raftserver
go run ../tests/raftserver/test_api.go
```

### 调试模式
```bash
cd /path/to/ConcordKV/raftserver
chmod +x ../tests/raftserver/debug.sh
../tests/raftserver/debug.sh
```

## 测试覆盖范围

### Raft协议功能
- 领导者选举
- 日志复制
- 安全性保证
- 状态机应用

### API服务器功能
- RESTful API接口
- JSON数据格式
- 错误处理
- 状态查询

### 键值存储功能
- SET操作
- GET操作
- DELETE操作
- 键列表查询
- 数据持久化

### 监控和调试功能
- 系统指标查询
- 日志查询
- 状态监控

## 测试环境要求

- Go 1.x 或更高版本
- Linux/Unix环境
- 可用的网络端口（默认使用21080-25081范围）
- curl命令行工具
- netstat命令（用于端口检查）

## 注意事项

1. 运行测试前请确保没有其他进程占用测试端口
2. 测试脚本会自动编译和启动服务器
3. 测试完成后会自动清理进程和临时文件
4. 如果测试失败，请查看生成的日志文件进行调试

## 测试结果

最新的测试结果显示：
- 总测试数：10项
- 通过测试：10项
- 失败测试：0项
- 成功率：100%

详细的测试结果请参阅 `TEST_RESULTS.md` 文件。 