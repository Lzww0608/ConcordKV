#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAFTSERVER_DIR="$(cd "$SCRIPT_DIR/../../raftserver" && pwd)"

cd "$RAFTSERVER_DIR" || {
    echo "错误：无法切换到raftserver目录"
    exit 1
}

echo "当前目录: $(pwd)"

# 编译服务器
echo "编译服务器..."
if ! go build -o concord_raft cmd/server/main.go; then
    echo "错误：编译失败"
    exit 1
fi

echo "文件列表:"
ls -la concord_raft
echo "运行服务器..."
timeout 10s ./concord_raft -node node1 -listen :18080 -api :18081 