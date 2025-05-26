#!/bin/bash

echo "启动ConcordKV Raft服务器测试..."

# 切换到raftserver目录
cd ../../raftserver || {
    echo "错误：无法切换到raftserver目录"
    exit 1
}

# 编译服务器
echo "编译服务器..."
if ! go build -o concord_raft cmd/server/main.go; then
    echo "错误：编译失败"
    exit 1
fi

# 启动服务器
echo "启动服务器..."
./concord_raft -node node1 -listen :21080 -api :21081 > server.log 2>&1 &
SERVER_PID=$!

# 等待服务器启动和选举完成
echo "等待服务器启动和选举完成..."
sleep 12

# 测试状态API
echo "测试状态API..."
curl -s http://localhost:21081/api/status | head -10

echo ""
echo "测试键值操作..."

# 测试设置键值
echo "设置 key1=value1..."
curl -s -X POST http://localhost:21081/api/set -H "Content-Type: application/json" -d '{"key":"key1","value":"value1"}'

echo ""
echo "获取 key1..."
curl -s "http://localhost:21081/api/get?key=key1"

echo ""
echo "设置 key2=value2..."
curl -s -X POST http://localhost:21081/api/set -H "Content-Type: application/json" -d '{"key":"key2","value":"value2"}'

echo ""
echo "获取所有键..."
curl -s http://localhost:21081/api/keys

echo ""
echo "删除 key1..."
curl -s -X DELETE "http://localhost:21081/api/delete?key=key1"

echo ""
echo "再次获取所有键..."
curl -s http://localhost:21081/api/keys

echo ""
echo "停止服务器..."
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null

echo ""
echo "测试完成！" 