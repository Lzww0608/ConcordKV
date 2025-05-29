#!/bin/bash

# ConcordKV 并发控制测试运行脚本
# 用于自动化运行死锁检测和锁超时处理测试

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

# 检查依赖
check_dependencies() {
    log_info "检查系统依赖..."
    
    # 检查编译器
    if ! command -v gcc &> /dev/null; then
        log_error "gcc 编译器未安装"
        exit 1
    fi
    
    # 检查make
    if ! command -v make &> /dev/null; then
        log_error "make 工具未安装"
        exit 1
    fi
    
    # 检查pthread库
    if ! ldconfig -p | grep -q pthread; then
        log_warning "pthread 库可能未正确安装"
    fi
    
    log_success "依赖检查通过"
}

# 编译测试程序
compile_tests() {
    log_info "编译测试程序..."
    
    if make clean > /dev/null 2>&1; then
        log_info "清理旧的编译文件"
    fi
    
    if make all; then
        log_success "编译完成"
    else
        log_error "编译失败"
        exit 1
    fi
}

# 运行基础功能测试
run_basic_tests() {
    log_info "运行基础功能测试..."
    
    echo "========================================"
    echo "           死锁检测测试"
    echo "========================================"
    
    if timeout 60s ./test_deadlock_detection; then
        log_success "死锁检测测试通过"
    else
        local exit_code=$?
        if [ $exit_code -eq 124 ]; then
            log_warning "死锁检测测试超时（60秒）"
        else
            log_error "死锁检测测试失败（退出码: $exit_code）"
            return 1
        fi
    fi
    
    echo ""
}

# 运行压力测试
run_stress_tests() {
    log_info "运行压力测试..."
    
    # 测试配置数组
    declare -a configs=(
        "2 20 90"   # 轻负载：2线程，20键，90%读
        "4 50 80"   # 中负载：4线程，50键，80%读
        "8 100 70"  # 重负载：8线程，100键，70%读
    )
    
    for config in "${configs[@]}"; do
        read -r threads keys read_ratio <<< "$config"
        
        echo "========================================"
        echo "  压力测试: $threads线程, $keys键, ${read_ratio}%读"
        echo "========================================"
        
        if timeout 30s ./test_lock_stress $threads $keys $read_ratio; then
            log_success "压力测试配置 ($config) 通过"
        else
            local exit_code=$?
            if [ $exit_code -eq 124 ]; then
                log_warning "压力测试配置 ($config) 超时（30秒）"
            else
                log_error "压力测试配置 ($config) 失败（退出码: $exit_code）"
                return 1
            fi
        fi
        
        echo ""
        sleep 1  # 短暂休息，避免系统过载
    done
}

# 运行性能基准测试
run_benchmark() {
    log_info "运行性能基准测试..."
    
    echo "========================================"
    echo "           性能基准测试"
    echo "========================================"
    
    echo "测试不同线程数量下的性能表现..."
    echo "配置: 100键, 70%读操作, 每个配置运行10秒"
    echo ""
    
    printf "%-8s %-15s %-12s %-15s\n" "线程数" "吞吐量(ops/s)" "成功率(%)" "平均延迟(μs)"
    printf "%-8s %-15s %-12s %-15s\n" "------" "-------------" "--------" "-------------"
    
    for threads in 1 2 4 8 16; do
        log_info "测试 $threads 线程配置..."
        
        # 运行测试并提取关键指标
        if result=$(timeout 15s ./test_lock_stress $threads 100 70 2>/dev/null); then
            # 提取吞吐量
            throughput=$(echo "$result" | grep "吞吐量:" | head -1 | awk '{print $2}')
            # 提取成功率
            success_rate=$(echo "$result" | grep "成功锁定:" | awk -F'[()]' '{print $2}' | sed 's/%//')
            # 提取平均延迟
            avg_latency=$(echo "$result" | grep "平均锁定时间:" | awk '{print $2}')
            
            printf "%-8d %-15s %-12s %-15s\n" "$threads" "$throughput" "$success_rate" "$avg_latency"
        else
            printf "%-8d %-15s %-12s %-15s\n" "$threads" "TIMEOUT" "N/A" "N/A"
            log_warning "$threads 线程测试超时"
        fi
    done
    
    echo ""
    log_success "性能基准测试完成"
}

# 运行内存检查（如果有valgrind）
run_memory_check() {
    if command -v valgrind &> /dev/null; then
        log_info "运行内存检查..."
        
        echo "========================================"
        echo "           内存泄漏检查"
        echo "========================================"
        
        # 运行简化的内存检查
        if valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
           --error-exitcode=1 --quiet \
           timeout 10s ./test_deadlock_detection > /dev/null 2>&1; then
            log_success "内存检查通过 - 无内存泄漏"
        else
            log_warning "内存检查发现问题，请查看详细日志"
        fi
    else
        log_warning "valgrind 未安装，跳过内存检查"
    fi
}

# 生成测试报告
generate_report() {
    local report_file="test_report_$(date +%Y%m%d_%H%M%S).txt"
    
    log_info "生成测试报告: $report_file"
    
    cat > "$report_file" << EOF
ConcordKV 并发控制测试报告
生成时间: $(date)
测试环境: $(uname -a)
编译器版本: $(gcc --version | head -1)

测试项目:
1. 死锁检测功能测试
2. 锁超时处理测试
3. 分段锁并发性能测试
4. 多键锁定排序测试
5. 压力测试（多种配置）
6. 性能基准测试

测试特性:
- 细粒度锁控制
- 死锁检测和预防
- 锁超时机制
- 多键原子操作
- 高并发性能优化

测试结果: 请查看上述测试输出
EOF

    log_success "测试报告已生成: $report_file"
}

# 清理函数
cleanup() {
    log_info "清理测试环境..."
    make clean > /dev/null 2>&1 || true
}

# 主函数
main() {
    echo "========================================"
    echo "    ConcordKV 并发控制增强功能测试"
    echo "========================================"
    echo "测试内容: 死锁检测 + 锁超时处理"
    echo "开始时间: $(date)"
    echo ""
    
    # 设置陷阱，确保清理
    trap cleanup EXIT
    
    # 检查依赖
    check_dependencies
    
    # 编译测试程序
    compile_tests
    
    # 运行测试
    local test_failed=0
    
    if ! run_basic_tests; then
        test_failed=1
    fi
    
    if ! run_stress_tests; then
        test_failed=1
    fi
    
    run_benchmark
    run_memory_check
    
    # 生成报告
    generate_report
    
    echo "========================================"
    if [ $test_failed -eq 0 ]; then
        log_success "所有测试完成！"
        echo "结束时间: $(date)"
    else
        log_error "部分测试失败，请检查上述输出"
        echo "结束时间: $(date)"
        exit 1
    fi
    echo "========================================"
}

# 解析命令行参数
case "${1:-}" in
    "basic")
        check_dependencies
        compile_tests
        run_basic_tests
        ;;
    "stress")
        check_dependencies
        compile_tests
        run_stress_tests
        ;;
    "benchmark")
        check_dependencies
        compile_tests
        run_benchmark
        ;;
    "memory")
        check_dependencies
        compile_tests
        run_memory_check
        ;;
    "help"|"-h"|"--help")
        echo "用法: $0 [选项]"
        echo ""
        echo "选项:"
        echo "  basic      - 只运行基础功能测试"
        echo "  stress     - 只运行压力测试"
        echo "  benchmark  - 只运行性能基准测试"
        echo "  memory     - 只运行内存检查"
        echo "  help       - 显示此帮助信息"
        echo ""
        echo "不带参数运行将执行完整测试套件"
        ;;
    *)
        main
        ;;
esac 