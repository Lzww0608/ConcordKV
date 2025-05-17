#!/bin/bash

set -e

# 切换到测试目录
cd "$(dirname "$0")"

# 下载Go依赖
go mod tidy

echo "=== 运行事务隔离级别测试 ==="
go test -v ./concord/...

echo "=== 测试完成 ===" 