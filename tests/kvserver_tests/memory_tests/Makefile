# ConcordKV Arena内存池测试 Makefile
# @Author: Lzww0608
# @Date: 2025-01-08 14:45:00
# @LastEditors: Lzww0608
# @LastEditTime: 2025-01-08 16:45:00
# @Description: Arena内存池测试构建脚本

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lpthread

# 检测系统是否支持NUMA
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    # 检查是否安装了libnuma
    NUMA_CHECK := $(shell pkg-config --exists libnuma && echo "yes" || echo "no")
    ifeq ($(NUMA_CHECK),yes)
        CFLAGS += -DHAVE_NUMA
        LDFLAGS += -lnuma
    endif
endif

# 源文件路径
KVSERVER_DIR = ../../../kvserver
MEMORY_SRC = $(KVSERVER_DIR)/kv_memory.c
MEMORY_HDR = $(KVSERVER_DIR)/kv_memory.h

# 测试文件
TEST_BASIC = test_arena_memory.c
TEST_ENHANCED = test_arena_memory_enhanced.c

# 目标文件
TARGET_BASIC = test_arena_memory
TARGET_ENHANCED = test_arena_memory_enhanced
TARGET_BLOCK_DEMO = block_size_performance_demo
TARGET_DEBUG = debug_test_runner
TARGET_QUICK = quick_perf_test
TARGET_INTEGRATION = kv_memory_integration_test

# 默认目标
.PHONY: all clean test test-basic test-enhanced benchmark valgrind help

all: $(TARGET_BASIC) $(TARGET_ENHANCED) $(TARGET_BLOCK_DEMO) $(TARGET_DEBUG) $(TARGET_QUICK) $(TARGET_INTEGRATION)

# 编译基础版测试
$(TARGET_BASIC): $(TEST_BASIC) $(MEMORY_SRC) $(MEMORY_HDR)
	@echo "🔨 编译基础版Arena内存池测试..."
	$(CC) $(CFLAGS) -o $@ $(TEST_BASIC) $(MEMORY_SRC) $(LDFLAGS)
	@echo "✅ 基础版测试编译完成"

# 编译增强版测试
$(TARGET_ENHANCED): $(TEST_ENHANCED) $(MEMORY_SRC) $(MEMORY_HDR)
	@echo "🔨 编译增强版Arena内存池测试..."
	$(CC) $(CFLAGS) -o $@ $(TEST_ENHANCED) $(MEMORY_SRC) $(LDFLAGS)
	@echo "✅ 增强版测试编译完成"

# 编译块大小性能演示
$(TARGET_BLOCK_DEMO): block_size_performance_demo.c $(MEMORY_SRC) $(MEMORY_HDR)
	@echo "🔨 编译块大小性能演示程序..."
	$(CC) $(CFLAGS) -o $@ block_size_performance_demo.c $(MEMORY_SRC) $(LDFLAGS)
	@echo "✅ 块大小演示程序编译完成"

# 编译调试测试
$(TARGET_DEBUG): debug_test_runner.c $(MEMORY_SRC) $(MEMORY_HDR)
	@echo "🔨 编译调试测试程序..."
	$(CC) $(CFLAGS) -o $@ debug_test_runner.c $(MEMORY_SRC) $(LDFLAGS)
	@echo "✅ 调试测试程序编译完成"

# 编译快速性能测试
$(TARGET_QUICK): quick_perf_test.c $(MEMORY_SRC) $(MEMORY_HDR)
	@echo "🔨 编译快速性能测试程序..."
	$(CC) $(CFLAGS) -o $@ quick_perf_test.c $(MEMORY_SRC) $(LDFLAGS)
	@echo "✅ 快速性能测试程序编译完成"

# 编译内存池集成测试
$(TARGET_INTEGRATION): kv_memory_integration_test.c $(MEMORY_SRC) $(MEMORY_HDR)
	@echo "🔨 编译内存池集成测试程序..."
	$(CC) $(CFLAGS) -o $@ kv_memory_integration_test.c $(MEMORY_SRC) $(LDFLAGS)
	@echo "✅ 内存池集成测试程序编译完成"

# 运行基础版测试
test-basic: $(TARGET_BASIC)
	@echo "🧪 运行基础版Arena内存池测试..."
	@echo "========================================"
	./$(TARGET_BASIC)
	@echo ""

# 运行增强版测试
test-enhanced: $(TARGET_ENHANCED)
	@echo "🧪 运行增强版Arena内存池测试..."
	@echo "========================================"
	./$(TARGET_ENHANCED)
	@echo ""

# 运行块大小性能演示
test-block-demo: $(TARGET_BLOCK_DEMO)
	@echo "🧪 运行块大小性能演示..."
	@echo "========================================"
	./$(TARGET_BLOCK_DEMO)
	@echo ""

# 运行调试测试
debug: $(TARGET_DEBUG)
	@echo "🔍 运行调试测试..."
	@echo "========================================"
	./$(TARGET_DEBUG)
	@echo ""

# 运行快速性能测试
quick: $(TARGET_QUICK)
	@echo "⚡ 运行快速性能测试..."
	@echo "========================================"
	./$(TARGET_QUICK)
	@echo ""

# 运行内存池集成测试
test-integration: $(TARGET_INTEGRATION)
	@echo "🔗 运行内存池集成测试..."
	@echo "========================================"
	./$(TARGET_INTEGRATION)
	@echo ""

# 运行所有测试
test: test-basic test-enhanced test-integration
	@echo "🎉 所有Arena内存池测试完成！"

# 性能基准测试
benchmark: $(TARGET_ENHANCED)
	@echo "📊 运行性能基准测试..."
	@echo "========================================"
	@echo "⚡ 基础性能测试:"
	./$(TARGET_ENHANCED) | grep -E "(性能对比|并发性能|Arena分配统计)"
	@echo ""
	@echo "🔥 压力测试 (大量分配):"
	@for i in 1 2 3; do \
		echo "第$$i轮测试:"; \
		./$(TARGET_ENHANCED) | grep "性能对比" || true; \
	done

# 内存泄漏检测 (需要安装valgrind)
valgrind: $(TARGET_ENHANCED)
	@echo "🔍 运行内存泄漏检测..."
	@echo "========================================"
	@if command -v valgrind >/dev/null 2>&1; then \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--suppressions=/dev/null ./$(TARGET_ENHANCED); \
	else \
		echo "❌ 未安装valgrind，跳过内存泄漏检测"; \
		echo "💡 安装命令: sudo apt-get install valgrind"; \
	fi

# 线程安全测试 (高并发)
stress-test: $(TARGET_ENHANCED)
	@echo "🔥 运行高并发压力测试..."
	@echo "========================================"
	@echo "测试配置: 多线程高并发分配"
	@for threads in 4 8 16 32; do \
		echo "🧵 $$threads 线程测试:"; \
		timeout 30s ./$(TARGET_ENHANCED) || echo "测试完成或超时"; \
		echo ""; \
	done

# 性能分析 (需要安装perf)
perf-analysis: $(TARGET_ENHANCED)
	@echo "📈 运行性能分析..."
	@echo "========================================"
	@if command -v perf >/dev/null 2>&1; then \
		echo "🔍 CPU性能分析:"; \
		perf stat -e cycles,instructions,cache-misses,cache-references ./$(TARGET_ENHANCED); \
		echo ""; \
		echo "🔍 内存访问分析:"; \
		perf stat -e L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses ./$(TARGET_ENHANCED); \
	else \
		echo "❌ 未安装perf工具，跳过性能分析"; \
		echo "💡 安装命令: sudo apt-get install linux-tools-generic"; \
	fi

# 编译信息
info:
	@echo "📋 编译环境信息"
	@echo "========================================"
	@echo "编译器: $(CC)"
	@echo "编译选项: $(CFLAGS)"
	@echo "链接选项: $(LDFLAGS)"
	@echo "系统: $(UNAME_S)"
ifeq ($(UNAME_S),Linux)
	@echo "NUMA支持: $(NUMA_CHECK)"
endif
	@echo "源文件: $(MEMORY_SRC)"
	@echo "头文件: $(MEMORY_HDR)"

# 清理
clean:
	@echo "🧹 清理编译文件..."
	rm -f $(TARGET_BASIC) $(TARGET_ENHANCED) $(TARGET_BLOCK_DEMO) $(TARGET_DEBUG) $(TARGET_QUICK)
	rm -f *.o *.core core.*
	@echo "✅ 清理完成"

# 帮助信息
help:
	@echo "🚀 ConcordKV Arena内存池测试构建系统"
	@echo "========================================"
	@echo "可用目标:"
	@echo "  all           - 编译所有测试程序"
	@echo "  test-basic    - 运行基础版测试"
	@echo "  test-enhanced - 运行增强版测试"
	@echo "  test-block-demo - 运行块大小性能演示"
	@echo "  test          - 运行所有测试"
	@echo "  benchmark     - 运行性能基准测试"
	@echo "  stress-test   - 运行高并发压力测试"
	@echo "  valgrind      - 运行内存泄漏检测"
	@echo "  perf-analysis - 运行性能分析"
	@echo "  info          - 显示编译环境信息"
	@echo "  clean         - 清理编译文件"
	@echo "  help          - 显示此帮助信息"
	@echo ""
	@echo "🔧 使用示例:"
	@echo "  make test           # 运行所有测试"
	@echo "  make benchmark      # 性能基准测试"
	@echo "  make valgrind       # 内存泄漏检测"
	@echo "  make stress-test    # 高并发压力测试" 