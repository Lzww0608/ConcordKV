#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 测试配置
LISTEN_PORT=21080
API_PORT=21081
NODE_ID="node1"
TIMEOUT=30
TEST_TIMEOUT=10

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# 清理函数
cleanup() {
    log_info "清理环境..."
    if [ ! -z "$SERVER_PID" ]; then
        log_info "停止服务器进程 $SERVER_PID"
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
    fi
    
    # 强制杀死可能残留的进程
    pkill -f concord_raft 2>/dev/null
    
    # 清理日志文件
    rm -f server_test.log
}

# 设置信号处理
trap cleanup EXIT INT TERM

# 检查端口是否被占用
check_port() {
    local port=$1
    if netstat -tlnp 2>/dev/null | grep -q ":$port "; then
        log_error "端口 $port 已被占用"
        return 1
    fi
    return 0
}

# 等待端口可用
wait_for_port() {
    local port=$1
    local timeout=$2
    local count=0
    
    log_info "等待端口 $port 可用..."
    while [ $count -lt $timeout ]; do
        if netstat -tlnp 2>/dev/null | grep -q ":$port "; then
            log_success "端口 $port 已可用"
            return 0
        fi
        sleep 1
        count=$((count + 1))
    done
    
    log_error "等待端口 $port 超时"
    return 1
}

# HTTP请求函数（带超时）
http_request() {
    local method=$1
    local url=$2
    local data=$3
    local expected_status=${4:-200}
    
    if [ -z "$data" ]; then
        response=$(curl -s -w "\n%{http_code}" --connect-timeout $TEST_TIMEOUT --max-time $TEST_TIMEOUT -X "$method" "$url" 2>/dev/null)
    else
        response=$(curl -s -w "\n%{http_code}" --connect-timeout $TEST_TIMEOUT --max-time $TEST_TIMEOUT -X "$method" -H "Content-Type: application/json" -d "$data" "$url" 2>/dev/null)
    fi
    
    if [ $? -ne 0 ]; then
        log_error "HTTP请求失败: $method $url"
        return 1
    fi
    
    # 分离响应体和状态码
    body=$(echo "$response" | head -n -1)
    status=$(echo "$response" | tail -n 1)
    
    if [ "$status" != "$expected_status" ]; then
        log_error "HTTP状态码错误: 期望 $expected_status, 实际 $status"
        log_error "响应体: $body"
        return 1
    fi
    
    echo "$body"
    return 0
}

# 验证JSON响应
validate_json() {
    local json=$1
    local key=$2
    local expected_value=$3
    
    if command -v jq >/dev/null 2>&1; then
        actual_value=$(echo "$json" | jq -r ".$key" 2>/dev/null)
        if [ "$actual_value" = "$expected_value" ]; then
            return 0
        else
            log_error "JSON验证失败: $key 期望 '$expected_value', 实际 '$actual_value'"
            return 1
        fi
    else
        # 简单的字符串匹配（如果没有jq）
        if echo "$json" | grep -q "\"$key\":\"$expected_value\""; then
            return 0
        else
            log_error "JSON验证失败: 未找到 $key:$expected_value"
            return 1
        fi
    fi
}

# 主测试函数
main() {
    log_info "开始ConcordKV Raft服务器测试..."
    
    # 切换到raftserver目录
    cd ../../raftserver || {
        log_error "无法切换到raftserver目录"
        exit 1
    }
    
    # 编译服务器
    log_info "编译服务器..."
    if ! go build -o concord_raft cmd/server/main.go; then
        log_error "编译失败"
        exit 1
    fi
    
    # 检查端口
    if ! check_port $LISTEN_PORT || ! check_port $API_PORT; then
        log_error "端口检查失败，请确保端口未被占用"
        exit 1
    fi
    
    # 启动服务器
    log_info "启动服务器..."
    ./concord_raft -node $NODE_ID -listen :$LISTEN_PORT -api :$API_PORT > server_test.log 2>&1 &
    SERVER_PID=$!
    
    if [ -z "$SERVER_PID" ]; then
        log_error "启动服务器失败"
        exit 1
    fi
    
    log_info "服务器进程ID: $SERVER_PID"
    
    # 等待服务器启动
    if ! wait_for_port $API_PORT $TIMEOUT; then
        log_error "服务器启动超时"
        cat server_test.log
        exit 1
    fi
    
    # 等待选举完成
    log_info "等待选举完成..."
    sleep 10
    
    # 测试计数器
    local tests_passed=0
    local tests_total=0
    
    # 测试1: 状态API
    log_info "测试1: 状态API"
    tests_total=$((tests_total + 1))
    if response=$(http_request "GET" "http://localhost:$API_PORT/api/status"); then
        log_success "状态API响应: $response"
        if echo "$response" | grep -q "node1"; then
            log_success "状态API测试通过"
            tests_passed=$((tests_passed + 1))
        else
            log_error "状态API响应格式错误"
        fi
    else
        log_error "状态API测试失败"
    fi
    
    # 测试2: 设置键值
    log_info "测试2: 设置键值 key1=value1"
    tests_total=$((tests_total + 1))
    if response=$(http_request "POST" "http://localhost:$API_PORT/api/set" '{"key":"key1","value":"value1"}'); then
        log_success "设置键值响应: $response"
        tests_passed=$((tests_passed + 1))
    else
        log_error "设置键值测试失败"
    fi
    
    # 测试3: 获取键值
    log_info "测试3: 获取键值 key1"
    tests_total=$((tests_total + 1))
    if response=$(http_request "GET" "http://localhost:$API_PORT/api/get?key=key1"); then
        log_success "获取键值响应: $response"
        if echo "$response" | grep -q "value1"; then
            log_success "获取键值测试通过"
            tests_passed=$((tests_passed + 1))
        else
            log_error "获取的值不正确"
        fi
    else
        log_error "获取键值测试失败"
    fi
    
    # 测试4: 设置第二个键值
    log_info "测试4: 设置键值 key2=value2"
    tests_total=$((tests_total + 1))
    if response=$(http_request "POST" "http://localhost:$API_PORT/api/set" '{"key":"key2","value":"value2"}'); then
        log_success "设置第二个键值响应: $response"
        tests_passed=$((tests_passed + 1))
    else
        log_error "设置第二个键值测试失败"
    fi
    
    # 测试5: 获取所有键
    log_info "测试5: 获取所有键"
    tests_total=$((tests_total + 1))
    if response=$(http_request "GET" "http://localhost:$API_PORT/api/keys"); then
        log_success "获取所有键响应: $response"
        if echo "$response" | grep -q "key1" && echo "$response" | grep -q "key2"; then
            log_success "获取所有键测试通过"
            tests_passed=$((tests_passed + 1))
        else
            log_error "获取的键列表不完整"
        fi
    else
        log_error "获取所有键测试失败"
    fi
    
    # 测试6: 删除键
    log_info "测试6: 删除键 key1"
    tests_total=$((tests_total + 1))
    if response=$(http_request "DELETE" "http://localhost:$API_PORT/api/delete?key=key1"); then
        log_success "删除键响应: $response"
        tests_passed=$((tests_passed + 1))
    else
        log_error "删除键测试失败"
    fi
    
    # 测试7: 验证删除结果
    log_info "测试7: 验证删除结果"
    tests_total=$((tests_total + 1))
    if response=$(http_request "GET" "http://localhost:$API_PORT/api/keys"); then
        log_success "验证删除后的键列表: $response"
        if echo "$response" | grep -q "key2" && ! echo "$response" | grep -q "key1"; then
            log_success "删除验证测试通过"
            tests_passed=$((tests_passed + 1))
        else
            log_error "删除验证失败"
        fi
    else
        log_error "删除验证测试失败"
    fi
    
    # 测试8: 获取不存在的键
    log_info "测试8: 获取不存在的键"
    tests_total=$((tests_total + 1))
    if response=$(http_request "GET" "http://localhost:$API_PORT/api/get?key=nonexistent" "" 404); then
        log_success "获取不存在键的响应正确: $response"
        tests_passed=$((tests_passed + 1))
    else
        log_error "获取不存在键的测试失败"
    fi
    
    # 输出测试结果
    echo ""
    log_info "========== 测试结果 =========="
    log_info "总测试数: $tests_total"
    log_info "通过测试: $tests_passed"
    log_info "失败测试: $((tests_total - tests_passed))"
    
    if [ $tests_passed -eq $tests_total ]; then
        log_success "所有测试通过！"
        exit 0
    else
        log_error "部分测试失败！"
        echo ""
        log_info "服务器日志:"
        cat server_test.log
        exit 1
    fi
}

# 运行主函数
main "$@" 