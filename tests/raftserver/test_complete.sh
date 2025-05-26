#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 测试配置
LISTEN_PORT=25080
API_PORT=25081
NODE_ID="test-node"
TIMEOUT=30
TEST_TIMEOUT=10

# 计数器
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
    ((PASSED_TESTS++))
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
    ((FAILED_TESTS++))
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# 测试函数
test_case() {
    local test_name="$1"
    local endpoint="$2"
    local method="$3"
    local data="$4"
    local expected_status="$5"
    
    ((TOTAL_TESTS++))
    log_info "测试: $test_name"
    
    # 直接调用test_api函数
    local result
    if result=$(test_api "$endpoint" "$method" "$data" "$expected_status" 2>&1); then
        log_success "$test_name - 通过"
        echo "响应: $result"
        return 0
    else
        log_error "$test_name - 失败"
        echo "错误: $result"
        return 1
    fi
}

# API测试函数
test_api() {
    local endpoint="$1"
    local method="$2"
    local data="$3"
    local expected_status="$4"
    
    local url="http://127.0.0.1:$API_PORT$endpoint"
    local curl_cmd="curl -s -w '%{http_code}' -X $method"
    
    if [ "$method" = "POST" ] && [ -n "$data" ]; then
        curl_cmd="$curl_cmd -H 'Content-Type: application/json' -d '$data'"
    fi
    
    curl_cmd="$curl_cmd $url"
    
    local response
    response=$(eval $curl_cmd)
    local http_code="${response: -3}"
    local body="${response%???}"
    
    if [ "$http_code" = "$expected_status" ]; then
        echo "$body"
        return 0
    else
        echo "HTTP错误: 期望 $expected_status, 实际 $http_code"
        echo "响应: $body"
        return 1
    fi
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
    
    # 清理临时文件
    rm -f /tmp/test_output server_test.log
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

# 等待服务器启动
wait_for_server() {
    local max_attempts=30
    local attempt=0
    
    while [ $attempt -lt $max_attempts ]; do
        if curl -s http://127.0.0.1:$API_PORT/api/status > /dev/null 2>&1; then
            return 0
        fi
        sleep 1
        ((attempt++))
    done
    
    return 1
}

# 主测试流程
main() {
    log_info "开始ConcordKV Raft服务器完整测试"
    log_info "========================================"
    
    # 检查端口
    if ! check_port $LISTEN_PORT || ! check_port $API_PORT; then
        log_error "端口检查失败"
        exit 1
    fi
    
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
    log_success "编译成功"
    
    # 启动服务器
    log_info "启动服务器..."
    ./concord_raft -node $NODE_ID -listen 127.0.0.1:$LISTEN_PORT -api 127.0.0.1:$API_PORT > server_test.log 2>&1 &
    SERVER_PID=$!
    
    # 等待服务器启动
    log_info "等待服务器启动..."
    if ! wait_for_server; then
        log_error "服务器启动失败或超时"
        cat server_test.log
        exit 1
    fi
    log_success "服务器启动成功"
    
    # 等待选举完成
    log_info "等待选举完成..."
    sleep 12
    
    # 测试状态API
    test_case "状态API测试" "/api/status" "GET" "" "200"
    
    # 测试键值API - 获取所有键
    test_case "获取所有键测试" "/api/keys" "GET" "" "200"
    
    # 测试键值API - 设置键值
    test_case "设置键值测试" "/api/set" "POST" '{"key":"test1","value":"value1"}' "200"
    
    # 测试键值API - 获取键值
    test_case "获取键值测试" "/api/get?key=test1" "GET" "" "200"
    
    # 测试键值API - 设置更多键值
    test_case "设置更多键值测试" "/api/set" "POST" '{"key":"test2","value":"value2"}' "200"
    
    # 测试键值API - 获取所有键（应该有2个）
    test_case "获取所有键测试（有数据）" "/api/keys" "GET" "" "200"
    
    # 测试键值API - 删除键值
    test_case "删除键值测试" "/api/delete?key=test1" "DELETE" "" "200"
    
    # 测试键值API - 获取已删除的键值
    test_case "获取已删除键值测试" "/api/get?key=test1" "GET" "" "200"
    
    # 测试指标API
    test_case "指标API测试" "/api/metrics" "GET" "" "200"
    
    # 测试日志API
    test_case "日志API测试" "/api/logs" "GET" "" "200"
    
    # 验证最终状态
    log_info "验证最终状态..."
    local status_response
    status_response=$(test_api '/api/status' 'GET' '' '200')
    if echo "$status_response" | grep -q '"isLeader":true'; then
        log_info "✅ 节点确认为领导者"
    else
        log_error "节点不是领导者"
        echo "状态响应: $status_response"
    fi
    
    # 显示测试结果
    log_info "========================================"
    log_info "测试完成"
    log_info "总测试数: $TOTAL_TESTS"
    log_success "通过: $PASSED_TESTS"
    log_error "失败: $FAILED_TESTS"
    
    if [ $FAILED_TESTS -eq 0 ]; then
        log_success "所有测试通过！"
        exit 0
    else
        log_error "有 $FAILED_TESTS 个测试失败"
        exit 1
    fi
}

# 运行主函数
main "$@" 