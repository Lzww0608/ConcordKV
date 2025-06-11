#!/bin/bash
#
# @Author: Lzww0608
# @Date: 2025-6-11 16:24:55
# @LastEditors: Lzww0608
# @LastEditTime: 2025-6-11 16:24:57
# @Description: ConcordKV io_uring 构建和测试脚本
#

set -e  # 出错即退出

# 脚本路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印函数
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查系统依赖
check_dependencies() {
    print_info "检查系统依赖..."
    
    # 检查cmake
    if ! command -v cmake &> /dev/null; then
        print_error "cmake 未安装，请安装 cmake"
        exit 1
    fi
    
    # 检查pkg-config
    if ! command -v pkg-config &> /dev/null; then
        print_error "pkg-config 未安装，请安装 pkg-config"
        exit 1
    fi
    
    # 检查liburing
    if ! pkg-config --exists liburing; then
        print_error "liburing 未安装，正在尝试安装..."
        if command -v apt-get &> /dev/null; then
            print_info "使用 apt-get 安装 liburing-dev..."
            sudo apt-get update
            sudo apt-get install -y liburing-dev
        elif command -v yum &> /dev/null; then
            print_info "使用 yum 安装 liburing-devel..."
            sudo yum install -y liburing-devel
        else
            print_error "无法自动安装 liburing，请手动安装"
            exit 1
        fi
    fi
    
    # 检查内核版本（io_uring需要5.1+）
    KERNEL_VERSION=$(uname -r | cut -d. -f1-2)
    KERNEL_MAJOR=$(echo $KERNEL_VERSION | cut -d. -f1)
    KERNEL_MINOR=$(echo $KERNEL_VERSION | cut -d. -f2)
    
    if [ "$KERNEL_MAJOR" -lt 5 ] || ([ "$KERNEL_MAJOR" -eq 5 ] && [ "$KERNEL_MINOR" -lt 1 ]); then
        print_warning "内核版本 $KERNEL_VERSION 可能不支持 io_uring（需要 5.1+）"
    else
        print_success "内核版本 $KERNEL_VERSION 支持 io_uring"
    fi
    
    print_success "依赖检查完成"
}

# 清理构建目录
clean_build() {
    print_info "清理构建目录..."
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
    fi
    mkdir -p "$BUILD_DIR"
    print_success "构建目录已清理"
}

# 构建项目
build_project() {
    print_info "开始构建项目..."
    
    cd "$BUILD_DIR"
    
    # 配置CMake
    print_info "配置 CMake..."
    cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    
    # 编译
    print_info "编译项目..."
    make -j$(nproc)
    
    print_success "项目构建完成"
}

# 运行测试
run_tests() {
    print_info "运行测试..."
    
    cd "$BUILD_DIR"
    
    # 运行基础测试
    print_info "运行基础功能测试..."
    if ./test_io_uring_basic; then
        print_success "基础功能测试通过"
    else
        print_error "基础功能测试失败"
        return 1
    fi
    
    # 运行CMake测试
    print_info "运行 CMake 测试..."
    if ctest --verbose --output-on-failure; then
        print_success "所有 CMake 测试通过"
    else
        print_error "CMake 测试失败"
        return 1
    fi
    
    print_success "所有测试完成"
}

# 显示使用说明
show_usage() {
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -c, --clean       仅清理构建目录"
    echo "  -b, --build       仅构建项目"
    echo "  -t, --test        仅运行测试"
    echo "  -a, --all         执行完整流程（默认）"
    echo "  -h, --help        显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                # 执行完整流程"
    echo "  $0 --clean        # 仅清理"
    echo "  $0 --build        # 仅构建"
    echo "  $0 --test         # 仅测试"
}

# 主函数
main() {
    print_info "ConcordKV io_uring 构建和测试脚本"
    print_info "=================================="
    
    # 解析命令行参数
    case "${1:-all}" in
        -c|--clean)
            check_dependencies
            clean_build
            ;;
        -b|--build)
            check_dependencies
            clean_build
            build_project
            ;;
        -t|--test)
            if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/test_io_uring_basic" ]; then
                print_error "项目尚未构建，请先运行构建"
                exit 1
            fi
            run_tests
            ;;
        -a|--all|"")
            check_dependencies
            clean_build
            build_project
            run_tests
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            print_error "未知选项: $1"
            show_usage
            exit 1
            ;;
    esac
    
    print_success "脚本执行完成"
}

# 错误处理
trap 'print_error "脚本执行过程中发生错误，退出码：$?"' ERR

# 执行主函数
main "$@" 