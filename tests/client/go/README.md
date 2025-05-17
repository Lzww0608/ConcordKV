# ConcordKV Go客户端测试

本目录包含ConcordKV Go客户端的测试代码，特别是事务隔离级别的测试实现。

## 目录结构

```
tests/client/go/
├── cmd                     # 命令行工具和示例
│   └── tx_isolation_demo   # 事务隔离级别演示
├── concord                 # 测试相关的核心代码
│   ├── mock                # 模拟实现，用于测试
│   ├── mock_client.go      # 模拟客户端实现
│   └── transaction_test.go # 事务隔离级别测试
├── go.mod                  # Go模块定义
└── run_tests.sh            # 测试运行脚本
```

## 运行测试

要运行所有测试，执行以下命令：

```bash
cd ConcordKV/tests/client/go
chmod +x run_tests.sh
./run_tests.sh
```

## 示例演示

要运行事务隔离级别演示程序：

```bash
cd ConcordKV/tests/client/go
go run cmd/tx_isolation_demo/main.go
```

## 测试内容

测试包括四种标准SQL事务隔离级别：

1. **读未提交（Read Uncommitted）**：验证能够读取到未提交的更改（脏读）
2. **读已提交（Read Committed）**：验证无法读取未提交更改，但能读取已提交更改（可能发生不可重复读）
3. **可重复读（Repeatable Read）**：验证同一事务内多次读取结果一致，即使其他事务已修改并提交数据
4. **串行化（Serializable）**：验证当读取的数据被其他事务修改时，事务提交会失败

这些测试验证了ConcordKV Go客户端实现的事务隔离级别行为符合预期。 