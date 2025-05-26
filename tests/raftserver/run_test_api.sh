#!/bin/bash

echo "运行API测试程序..."

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAFTSERVER_DIR="$(cd "$SCRIPT_DIR/../../raftserver" && pwd)"

# 切换到raftserver目录
cd "$RAFTSERVER_DIR" || {
    echo "错误：无法切换到raftserver目录"
    exit 1
}

echo "当前目录: $(pwd)"

# 运行测试程序
echo "运行Go测试程序..."
go run ../tests/raftserver/test_api.go 