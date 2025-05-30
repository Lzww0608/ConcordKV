#!/bin/bash

#
# @Author: Lzww0608
# @Date: 2025-5-30 09:56:35
# @LastEditors: Lzww0608
# @LastEditTime: 2025-5-30 11:17:49
# @Description: ConcordKV分布式事务测试运行脚本
#

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 脚本参数
MODE="all"
TIMEOUT=300  # 默认5分钟超时
VALGRIND=false
VERBOSE=false
REPORT=false
CLEAN=false
BUILD_TYPE="debug"

# 帮助信息
show_help() {
    cat << EOF
ConcordKV分布式事务测试运行脚本

用法: $0 [选项]

选项:
  -m, --mode MODE          测试模式 (all, distributed, priority, 2pc, timeout)
  -t, --timeout SECONDS    测试超时时间 (默认: 300秒)
  -v, --valgrind          使用Valgrind进行内存检查
  -V, --verbose           详细输出
  -r, --report            生成测试报告
  -c, --clean             运行前清理构建文件
  -b, --build-type TYPE   构建类型 (debug, release, memcheck)
  -h, --help              显示此帮助信息

示例:
  $0                      # 运行所有测试
  $0 -m priority          # 只运行优先级队列测试
  $0 -v -r                # 使用Valgrind运行并生成报告
  $0 -t 600 -m all        # 10分钟超时运行所有测试

EOF
}

# 解析命令行参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -m|--mode)
                MODE="$2"
                shift 2
                ;;
            -t|--timeout)
                TIMEOUT="$2"
                shift 2
                ;;
            -v|--valgrind)
                VALGRIND=true
                shift
                ;;
            -V|--verbose)
                VERBOSE=true
                shift
                ;;
            -r|--report)
                REPORT=true
                shift
                ;;
            -c|--clean)
                CLEAN=true
                shift
                ;;
            -b|--build-type)
                BUILD_TYPE="$2"
                shift 2
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                log_error "未知参数: $1"
                show_help
                exit 1
                ;;
        esac
    done
}

# 检查依赖
check_dependencies() {
    log_info "检查依赖..."
    
    # 检查编译器
    if ! command -v gcc &> /dev/null; then
        log_error "GCC编译器未找到"
        exit 1
    fi
    
    # 检查必要的库
    if ! pkg-config --exists threads; then
        log_warning "pthread库可能未正确安装"
    fi
    
    # 检查Valgrind（如果需要）
    if [[ "$VALGRIND" == true ]] && ! command -v valgrind &> /dev/null; then
        log_error "Valgrind未找到，但已请求内存检查"
        exit 1
    fi
    
    log_success "依赖检查完成"
}

# 清理构建文件
clean_build() {
    if [[ "$CLEAN" == true ]]; then
        log_info "清理构建文件..."
        make clean 2>/dev/null || true
        rm -rf build/ bin/ *.log *.out test_report.md 2>/dev/null || true
        log_success "清理完成"
    fi
}

# 构建测试
build_tests() {
    log_info "构建测试程序 (类型: $BUILD_TYPE)..."
    
    local build_target
    case "$BUILD_TYPE" in
        debug)
            build_target="debug"
            ;;
        release)
            build_target="release"
            ;;
        memcheck)
            build_target="memcheck"
            ;;
        *)
            log_error "未知构建类型: $BUILD_TYPE"
            exit 1
            ;;
    esac
    
    if ! make $build_target; then
        log_error "构建失败"
        exit 1
    fi
    
    log_success "构建完成"
}

# 运行单个测试
run_single_test() {
    local test_name="$1"
    local test_binary="./bin/test_$test_name"
    local log_file="test_${test_name}.log"
    
    log_info "运行测试: $test_name"
    
    if [[ ! -x "$test_binary" ]]; then
        log_error "测试二进制文件不存在: $test_binary"
        return 1
    fi
    
    local cmd="$test_binary"
    local timeout_cmd=""
    
    # 添加超时命令
    if command -v timeout &> /dev/null; then
        timeout_cmd="timeout ${TIMEOUT}s"
    elif command -v gtimeout &> /dev/null; then
        timeout_cmd="gtimeout ${TIMEOUT}s"
    fi
    
    # 添加Valgrind（如果需要）
    if [[ "$VALGRIND" == true ]]; then
        cmd="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 $cmd"
    fi
    
    # 添加报告参数（如果需要）
    if [[ "$REPORT" == true ]]; then
        cmd="$cmd --report"
    fi
    
    # 构建完整命令
    local full_cmd="$timeout_cmd $cmd"
    
    # 执行测试
    local start_time=$(date +%s)
    local exit_code=0
    
    if [[ "$VERBOSE" == true ]]; then
        eval $full_cmd 2>&1 | tee "$log_file"
        exit_code=${PIPESTATUS[0]}
    else
        eval $full_cmd > "$log_file" 2>&1
        exit_code=$?
    fi
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    # 检查结果
    case $exit_code in
        0)
            log_success "测试 $test_name 通过 (耗时: ${duration}秒)"
            return 0
            ;;
        124)
            log_error "测试 $test_name 超时 (超过 ${TIMEOUT}秒)"
            return 1
            ;;
        *)
            log_error "测试 $test_name 失败 (退出码: $exit_code, 耗时: ${duration}秒)"
            if [[ "$VERBOSE" == false ]]; then
                log_info "错误详情请查看: $log_file"
            fi
            return 1
            ;;
    esac
}

# 运行测试
run_tests() {
    local tests_to_run=()
    
    case "$MODE" in
        all)
            tests_to_run=("distributed_transaction" "priority_queue" "2pc_protocol" "timeout_handling")
            ;;
        distributed)
            tests_to_run=("distributed_transaction")
            ;;
        priority)
            tests_to_run=("priority_queue")
            ;;
        2pc)
            tests_to_run=("2pc_protocol")
            ;;
        timeout)
            tests_to_run=("timeout_handling")
            ;;
        *)
            log_error "未知测试模式: $MODE"
            exit 1
            ;;
    esac
    
    log_info "开始运行测试 (模式: $MODE, 超时: ${TIMEOUT}秒)"
    
    local total_tests=${#tests_to_run[@]}
    local passed_tests=0
    local failed_tests=0
    local start_time=$(date +%s)
    
    for test in "${tests_to_run[@]}"; do
        if run_single_test "$test"; then
            ((passed_tests++))
        else
            ((failed_tests++))
        fi
        echo  # 空行分隔
    done
    
    local end_time=$(date +%s)
    local total_duration=$((end_time - start_time))
    
    # 输出总结
    echo "=================================="
    log_info "测试总结:"
    echo "  总测试数: $total_tests"
    echo "  通过: $passed_tests"
    echo "  失败: $failed_tests"
    echo "  总耗时: ${total_duration}秒"
    echo "  成功率: $(( passed_tests * 100 / total_tests ))%"
    
    if [[ $failed_tests -eq 0 ]]; then
        log_success "所有测试都通过了！"
        return 0
    else
        log_error "有 $failed_tests 个测试失败"
        return 1
    fi
}

# 生成测试报告
generate_report() {
    if [[ "$REPORT" == true ]]; then
        log_info "生成测试报告..."
        
        local report_file="distributed_transaction_test_report.md"
        
        cat > "$report_file" << EOF
# ConcordKV分布式事务测试报告

生成时间: $(date)
测试模式: $MODE
构建类型: $BUILD_TYPE
使用Valgrind: $VALGRIND

## 测试环境

- 操作系统: $(uname -s)
- 内核版本: $(uname -r)
- 架构: $(uname -m)
- GCC版本: $(gcc --version | head -n1)
- 处理器: $(nproc) 核心

## 测试结果

EOF
        
        # 收集各个测试的报告内容
        for log_file in test_*.log; do
            if [[ -f "$log_file" ]]; then
                echo "### $(basename "$log_file" .log)" >> "$report_file"
                echo "" >> "$report_file"
                if grep -q "### " "$log_file"; then
                    grep -A 20 "### " "$log_file" >> "$report_file"
                else
                    echo "测试输出:" >> "$report_file"
                    echo '```' >> "$report_file"
                    tail -n 20 "$log_file" >> "$report_file"
                    echo '```' >> "$report_file"
                fi
                echo "" >> "$report_file"
            fi
        done
        
        cat >> "$report_file" << EOF

## 性能分析

$(if [[ "$VALGRIND" == true ]]; then
    echo "### 内存检查"
    echo "使用Valgrind进行了内存泄漏和错误检查。"
    echo ""
fi)

## 建议

1. 定期运行这些测试以确保代码质量
2. 在添加新功能时增加相应的测试用例
3. 监控测试性能，及时发现性能退化

---
*报告由 ConcordKV 分布式事务测试套件自动生成*
EOF
        
        log_success "测试报告已生成: $report_file"
    fi
}

# 清理临时文件
cleanup() {
    if [[ "$VERBOSE" == false ]]; then
        # 只在非详细模式下清理日志文件
        rm -f test_*.log 2>/dev/null || true
    fi
}

# 主函数
main() {
    echo "ConcordKV分布式事务测试套件"
    echo "========================="
    echo
    
    parse_args "$@"
    
    # 检查是否在正确目录
    if [[ ! -f "Makefile" ]]; then
        log_error "请在包含Makefile的测试目录中运行此脚本"
        exit 1
    fi
    
    check_dependencies
    clean_build
    build_tests
    
    # 设置信号处理
    trap cleanup EXIT
    
    local success=false
    if run_tests; then
        success=true
    fi
    
    generate_report
    
    if [[ "$success" == true ]]; then
        log_success "所有测试完成！"
        exit 0
    else
        log_error "测试失败！"
        exit 1
    fi
}

# 执行主函数
main "$@" 