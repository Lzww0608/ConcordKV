#!/bin/bash

#
# @Author: Lzww0608
# @Date: 2025-6-4 16:45:00
# @LastEditors: Lzww0608
# @LastEditTime: 2025-6-4 16:45:00
# @Description: LSM-Tree批量写入功能GTest自动化测试脚本
#
# 参考开源项目最佳实践：
# - RocksDB自动化测试脚本
# - LevelDB构建脚本
# - CMake最佳实践
#

set -e  # 遇到错误立即退出

# === 脚本配置 ===
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_gtest"
CMAKE_FILE="$SCRIPT_DIR/CMakeLists_gtest.txt"
TEST_EXECUTABLE="$BUILD_DIR/lsm_batch_write_gtest"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# === 辅助函数 ===

print_header() {
    echo -e "${BLUE}"
    echo "=============================================================="
    echo "$1"
    echo "=============================================================="
    echo -e "${NC}"
}

print_step() {
    echo -e "${CYAN}[步骤] $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_info() {
    echo -e "${PURPLE}📋 $1${NC}"
}

# 显示帮助信息
show_help() {
    cat << EOF
LSM-Tree批量写入功能GTest测试脚本

用法: $0 [选项] [测试过滤器]

选项:
    -h, --help          显示此帮助信息
    -c, --clean         清理构建目录
    -b, --build-only    仅构建，不运行测试
    -t, --test-only     仅运行测试（需要先构建）
    -v, --verbose       显示详细输出
    -d, --debug         使用Debug模式构建
    -j, --jobs N        使用N个并行作业构建（默认：4）
    
测试类型过滤器:
    basic              运行基础功能测试
    performance        运行性能测试
    concurrency        运行并发测试
    stress             运行压力测试
    wal                运行WAL测试
    error              运行错误处理测试
    all                运行所有测试（默认）

示例:
    $0                          # 构建并运行所有测试
    $0 basic                    # 运行基础功能测试
    $0 --clean performance      # 清理后运行性能测试
    $0 --debug --verbose        # Debug模式构建并显示详细输出
    $0 --test-only concurrency  # 仅运行并发测试

EOF
}

# 检查依赖
check_dependencies() {
    print_step "检查依赖项..."
    
    # 检查必需的工具
    local missing_deps=()
    
    if ! command -v cmake &> /dev/null; then
        missing_deps+=("cmake")
    fi
    
    if ! command -v g++ &> /dev/null; then
        missing_deps+=("g++")
    fi
    
    if ! command -v gcc &> /dev/null; then
        missing_deps+=("gcc")
    fi
    
    # 检查gtest是否安装
    if ! pkg-config --exists gtest; then
        if [ ! -f "/usr/include/gtest/gtest.h" ]; then
            missing_deps+=("libgtest-dev")
        fi
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "缺少依赖项: ${missing_deps[*]}"
        echo "请安装缺少的依赖项："
        echo "  sudo apt-get install cmake g++ gcc libgtest-dev libgmock-dev"
        exit 1
    fi
    
    print_success "所有依赖项检查通过"
}

# 检查源文件
check_source_files() {
    print_step "检查源文件..."
    
    local missing_files=()
    
    # 检查关键源文件
    local required_files=(
        "../../../kvserver/lsm_tree.h"
        "../../../kvserver/lsm_tree.c"
        "../../../kvserver/kv_memory.h"
        "../../../kvserver/kv_memory.c"
        "../../../kvserver/kv_error.h"
        "../../../kvserver/kv_error.c"
        "lsm_batch_write_gtest.cpp"
        "lsm_batch_write_gtest_main.cpp"
        "CMakeLists_gtest.txt"
    )
    
    for file in "${required_files[@]}"; do
        if [ ! -f "$SCRIPT_DIR/$file" ]; then
            missing_files+=("$file")
        fi
    done
    
    if [ ${#missing_files[@]} -ne 0 ]; then
        print_error "缺少源文件:"
        for file in "${missing_files[@]}"; do
            echo "  - $file"
        done
        exit 1
    fi
    
    print_success "所有源文件检查通过"
}

# 清理构建目录
clean_build() {
    print_step "清理构建目录..."
    
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        print_success "构建目录已清理"
    else
        print_info "构建目录不存在，无需清理"
    fi
    
    # 清理临时测试文件
    rm -rf "$SCRIPT_DIR"/gtest_lsm_batch_*
    rm -rf "$SCRIPT_DIR"/test_batch_*
    print_success "临时测试文件已清理"
}

# 构建项目
build_project() {
    print_step "构建项目..."
    
    # 创建构建目录
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # 配置CMake
    local cmake_args=(
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    )
    
    if [ "$VERBOSE" = true ]; then
        cmake_args+=(-DCMAKE_VERBOSE_MAKEFILE=ON)
    fi
    
    print_info "运行CMake配置..."
    if ! cmake "${cmake_args[@]}" "$SCRIPT_DIR"; then
        print_error "CMake配置失败"
        exit 1
    fi
    
    # 构建项目
    print_info "开始编译..."
    if ! make -j"$JOBS"; then
        print_error "编译失败"
        exit 1
    fi
    
    print_success "项目构建完成"
    
    # 检查可执行文件
    if [ ! -f "$TEST_EXECUTABLE" ]; then
        print_error "测试可执行文件未生成: $TEST_EXECUTABLE"
        exit 1
    fi
    
    print_success "测试可执行文件生成成功"
}

# 运行测试
run_tests() {
    print_step "运行测试..."
    
    if [ ! -f "$TEST_EXECUTABLE" ]; then
        print_error "测试可执行文件不存在，请先构建项目"
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    # 设置测试过滤器
    local gtest_filter=""
    case "$TEST_FILTER" in
        basic)
            gtest_filter="--gtest_filter=*Basic*"
            ;;
        performance)
            gtest_filter="--gtest_filter=*Performance*"
            ;;
        concurrency)
            gtest_filter="--gtest_filter=*Concurrent*"
            ;;
        stress)
            gtest_filter="--gtest_filter=*Stress*"
            ;;
        wal)
            gtest_filter="--gtest_filter=*WAL*"
            ;;
        error)
            gtest_filter="--gtest_filter=*Error*"
            ;;
        all|"")
            gtest_filter=""
            ;;
        *)
            gtest_filter="--gtest_filter=$TEST_FILTER"
            ;;
    esac
    
    # 设置测试参数
    local test_args=(
        --gtest_color=yes
        --gtest_print_time=1
    )
    
    if [ "$VERBOSE" = true ]; then
        test_args+=(--gtest_brief=0)
    fi
    
    if [ -n "$gtest_filter" ]; then
        test_args+=("$gtest_filter")
    fi
    
    print_info "测试参数: ${test_args[*]}"
    print_info "开始执行测试..."
    
    # 运行测试
    if "./lsm_batch_write_gtest" "${test_args[@]}"; then
        print_success "所有测试通过！"
        return 0
    else
        print_error "部分测试失败"
        return 1
    fi
}

# 显示构建信息
show_build_info() {
    print_info "构建配置信息:"
    echo "  • 构建目录: $BUILD_DIR"
    echo "  • 构建类型: $BUILD_TYPE"
    echo "  • 并行作业: $JOBS"
    echo "  • 详细输出: $VERBOSE"
    echo "  • 测试过滤器: ${TEST_FILTER:-"全部"}"
    echo ""
}

# === 主程序 ===

# 默认参数
BUILD_TYPE="Release"
JOBS=4
VERBOSE=false
CLEAN_ONLY=false
BUILD_ONLY=false
TEST_ONLY=false
TEST_FILTER=""

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -c|--clean)
            CLEAN_ONLY=true
            shift
            ;;
        -b|--build-only)
            BUILD_ONLY=true
            shift
            ;;
        -t|--test-only)
            TEST_ONLY=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        basic|performance|concurrency|stress|wal|error|all)
            TEST_FILTER="$1"
            shift
            ;;
        --gtest_filter=*)
            TEST_FILTER="${1#--gtest_filter=}"
            shift
            ;;
        *)
            # 如果不是已知选项，作为测试过滤器处理
            TEST_FILTER="$1"
            shift
            ;;
    esac
done

# 主程序流程
main() {
    print_header "ConcordKV LSM-Tree批量写入功能GTest测试"
    
    # 显示构建信息
    show_build_info
    
    # 检查依赖和源文件
    check_dependencies
    check_source_files
    
    # 仅清理模式
    if [ "$CLEAN_ONLY" = true ]; then
        clean_build
        print_success "清理完成"
        exit 0
    fi
    
    # 仅测试模式
    if [ "$TEST_ONLY" = true ]; then
        if run_tests; then
            exit 0
        else
            exit 1
        fi
    fi
    
    # 清理（如果需要）
    if [ "$BUILD_TYPE" = "Debug" ] || [ ! -d "$BUILD_DIR" ]; then
        clean_build
    fi
    
    # 构建项目
    build_project
    
    # 仅构建模式
    if [ "$BUILD_ONLY" = true ]; then
        print_success "构建完成"
        exit 0
    fi
    
    # 运行测试
    if run_tests; then
        print_success "测试执行完成"
        exit 0
    else
        print_error "测试执行失败"
        exit 1
    fi
}

# 执行主程序
main "$@" 