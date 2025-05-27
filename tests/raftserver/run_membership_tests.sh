#!/bin/bash

# 成员变更功能测试脚本
# 包含超时机制防止死锁

set -e

echo "=== ConcordKV 成员变更功能测试 ==="
echo "测试时间: $(date)"
echo

# 设置测试超时时间（秒）
TEST_TIMEOUT=300

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 超时执行函数
run_with_timeout() {
    local timeout=$1
    local cmd="$2"
    local desc="$3"
    
    log_info "开始执行: $desc"
    
    if timeout $timeout bash -c "$cmd"; then
        log_info "✓ $desc 执行成功"
        return 0
    else
        local exit_code=$?
        if [ $exit_code -eq 124 ]; then
            log_error "✗ $desc 执行超时 (${timeout}s)"
        else
            log_error "✗ $desc 执行失败 (退出码: $exit_code)"
        fi
        return $exit_code
    fi
}

# 检查Go环境
check_go_env() {
    log_info "检查Go环境..."
    
    if ! command -v go &> /dev/null; then
        log_error "Go未安装或不在PATH中"
        exit 1
    fi
    
    local go_version=$(go version)
    log_info "Go版本: $go_version"
}

# 检查项目结构
check_project_structure() {
    log_info "检查项目结构..."
    
    local required_dirs=(
        "../../raftserver/raft"
        "../../raftserver/server"
        "../../raftserver/storage"
    )
    
    for dir in "${required_dirs[@]}"; do
        if [ ! -d "$dir" ]; then
            log_warn "目录不存在: $dir"
        else
            log_info "✓ 目录存在: $dir"
        fi
    done
}

# 运行单元测试
run_unit_tests() {
    log_info "运行成员变更单元测试..."
    
    local test_cmd="cd $(dirname $0) && go test -v -timeout ${TEST_TIMEOUT}s ./membership_test.go"
    
    if run_with_timeout $TEST_TIMEOUT "$test_cmd" "成员变更单元测试"; then
        return 0
    else
        log_error "单元测试失败"
        return 1
    fi
}

# 运行API测试
run_api_tests() {
    log_info "运行成员变更API测试..."
    
    # 启动测试服务器（如果存在）
    local server_pid=""
    
    # 检查是否有可用的服务器二进制文件
    if [ -f "../../raftserver/server/server" ]; then
        log_info "启动测试服务器..."
        ../../raftserver/server/server &
        server_pid=$!
        sleep 2
        log_info "测试服务器已启动 (PID: $server_pid)"
    else
        log_warn "未找到服务器二进制文件，将运行模拟API测试"
    fi
    
    # 运行API测试
    local api_test_cmd="cd $(dirname $0) && go test -v -timeout ${TEST_TIMEOUT}s -run TestMembershipChangeAPI ./membership_test.go"
    
    local result=0
    if ! run_with_timeout $TEST_TIMEOUT "$api_test_cmd" "成员变更API测试"; then
        result=1
    fi
    
    # 清理测试服务器
    if [ ! -z "$server_pid" ]; then
        log_info "停止测试服务器..."
        kill $server_pid 2>/dev/null || true
        wait $server_pid 2>/dev/null || true
        log_info "测试服务器已停止"
    fi
    
    return $result
}

# 运行并发测试
run_concurrent_tests() {
    log_info "运行并发成员变更测试..."
    
    local concurrent_test_cmd="cd $(dirname $0) && go test -v -timeout ${TEST_TIMEOUT}s -run TestConcurrentMembershipChanges ./membership_test.go"
    
    if run_with_timeout $TEST_TIMEOUT "$concurrent_test_cmd" "并发成员变更测试"; then
        return 0
    else
        log_error "并发测试失败"
        return 1
    fi
}

# 运行超时测试
run_timeout_tests() {
    log_info "运行超时机制测试..."
    
    local timeout_test_cmd="cd $(dirname $0) && go test -v -timeout ${TEST_TIMEOUT}s -run TestMembershipChangeTimeout ./membership_test.go"
    
    if run_with_timeout $TEST_TIMEOUT "$timeout_test_cmd" "超时机制测试"; then
        return 0
    else
        log_error "超时测试失败"
        return 1
    fi
}

# 运行验证测试
run_validation_tests() {
    log_info "运行成员变更验证测试..."
    
    local validation_test_cmd="cd $(dirname $0) && go test -v -timeout ${TEST_TIMEOUT}s -run TestMembershipValidation ./membership_test.go"
    
    if run_with_timeout $TEST_TIMEOUT "$validation_test_cmd" "成员变更验证测试"; then
        return 0
    else
        log_error "验证测试失败"
        return 1
    fi
}

# 生成测试报告
generate_report() {
    local total_tests=$1
    local passed_tests=$2
    local failed_tests=$3
    
    echo
    echo "=== 测试报告 ==="
    echo "总测试数: $total_tests"
    echo "通过: $passed_tests"
    echo "失败: $failed_tests"
    echo "成功率: $(( passed_tests * 100 / total_tests ))%"
    echo "测试完成时间: $(date)"
    echo
    
    if [ $failed_tests -eq 0 ]; then
        log_info "🎉 所有测试通过！"
        return 0
    else
        log_error "❌ 有 $failed_tests 个测试失败"
        return 1
    fi
}

# 主函数
main() {
    local start_time=$(date +%s)
    local total_tests=0
    local passed_tests=0
    local failed_tests=0
    
    echo "开始时间: $(date)"
    echo
    
    # 环境检查
    check_go_env
    check_project_structure
    echo
    
    # 运行各种测试
    local tests=(
        "run_unit_tests:单元测试"
        "run_api_tests:API测试"
        "run_concurrent_tests:并发测试"
        "run_timeout_tests:超时测试"
        "run_validation_tests:验证测试"
    )
    
    for test_info in "${tests[@]}"; do
        local test_func="${test_info%%:*}"
        local test_name="${test_info##*:}"
        
        total_tests=$((total_tests + 1))
        
        echo "----------------------------------------"
        log_info "开始 $test_name"
        
        if $test_func; then
            passed_tests=$((passed_tests + 1))
            log_info "✓ $test_name 通过"
        else
            failed_tests=$((failed_tests + 1))
            log_error "✗ $test_name 失败"
        fi
        
        echo
    done
    
    # 生成报告
    echo "========================================"
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    log_info "总执行时间: ${duration}秒"
    
    generate_report $total_tests $passed_tests $failed_tests
}

# 信号处理
cleanup() {
    log_warn "收到中断信号，正在清理..."
    # 杀死所有子进程
    jobs -p | xargs -r kill
    exit 130
}

trap cleanup SIGINT SIGTERM

# 执行主函数
main "$@" 