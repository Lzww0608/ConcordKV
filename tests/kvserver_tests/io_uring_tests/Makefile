# ConcordKV io_uring 测试套件 Makefile

CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -std=c99 -D_GNU_SOURCE
INCLUDES = -I../../../kvserver -I../../../common
LIBS = -pthread -luring -lm

# 源文件目录
KVSERVER_DIR = ../../../kvserver
COMMON_DIR = ../../../common

# 基础源文件
KVSERVER_SOURCES = \
    $(KVSERVER_DIR)/kv_error.c \
    $(KVSERVER_DIR)/kv_memory.c \
    $(KVSERVER_DIR)/kv_io_uring.c \
    $(KVSERVER_DIR)/kv_async_batch_io.c

# 测试目标
TARGETS = test_io_uring_basic simple_io_test test_async_io_operations test_async_batch_io

.PHONY: all clean test run_basic_test run_simple_test run_async_operations run_async_batch build_all

all: $(TARGETS)

# 基础 io_uring 测试
test_io_uring_basic: test_io_uring_basic.c $(KVSERVER_SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# 简单 I/O 测试
simple_io_test: simple_io_test.c $(KVSERVER_SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# 异步 I/O 操作测试
test_async_io_operations: test_async_io_operations.c $(KVSERVER_SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# 异步批量 I/O 测试 (新增)
test_async_batch_io: test_async_batch_io.c $(KVSERVER_SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# 运行测试
run_basic_test: test_io_uring_basic
	@echo "=== 运行基础 io_uring 测试 ==="
	./test_io_uring_basic

run_simple_test: simple_io_test
	@echo "=== 运行简单 I/O 测试 ==="
	./simple_io_test

run_async_operations: test_async_io_operations
	@echo "=== 运行异步 I/O 操作测试 ==="
	./test_async_io_operations

run_async_batch: test_async_batch_io
	@echo "=== 运行异步批量 I/O 测试 ==="
	./test_async_batch_io

# 运行所有测试
test: $(TARGETS)
	@echo "=== 运行所有 io_uring 测试 ==="
	@echo ""
	@echo "1. 基础 io_uring 测试..."
	@./test_io_uring_basic && echo "✓ 基础测试通过" || echo "✗ 基础测试失败"
	@echo ""
	@echo "2. 简单 I/O 测试..."
	@./simple_io_test && echo "✓ 简单测试通过" || echo "✗ 简单测试失败"
	@echo ""
	@echo "3. 异步 I/O 操作测试..."
	@./test_async_io_operations && echo "✓ 异步操作测试通过" || echo "✗ 异步操作测试失败"
	@echo ""
	@echo "4. 异步批量 I/O 测试..."
	@./test_async_batch_io && echo "✓ 异步批量测试通过" || echo "✗ 异步批量测试失败"
	@echo ""
	@echo "=== 所有测试完成 ==="

# 构建所有目标但不运行测试
build_all: $(TARGETS)
	@echo "所有目标构建完成"

# 清理
clean:
	rm -f $(TARGETS) *.o
	rm -rf /tmp/kv_*_test
	@echo "清理完成"

# 帮助信息
help:
	@echo "可用的 make 目标："
	@echo "  all                 - 构建所有测试程序"
	@echo "  test                - 运行所有测试"
	@echo "  run_basic_test      - 运行基础 io_uring 测试"
	@echo "  run_simple_test     - 运行简单 I/O 测试"
	@echo "  run_async_operations - 运行异步 I/O 操作测试"
	@echo "  run_async_batch     - 运行异步批量 I/O 测试"
	@echo "  build_all          - 仅构建，不运行测试"
	@echo "  clean              - 清理构建文件"
	@echo "  help               - 显示此帮助信息"

# 检查依赖
check_deps:
	@echo "检查依赖..."
	@which gcc > /dev/null || (echo "错误: 未找到 gcc" && exit 1)
	@pkg-config --exists liburing || (echo "错误: 未找到 liburing" && exit 1)
	@echo "✓ 所有依赖都已满足"

.PHONY: help check_deps 