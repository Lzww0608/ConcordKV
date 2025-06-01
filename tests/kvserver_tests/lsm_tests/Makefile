# LSM-Tree MemTable 测试 Makefile
# @Author: Lzww0608
# @Date: 2025-12-19 15:30:00

CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -std=c99 -pthread
INCLUDES = -I../../../kvserver -I../../../common
LIBS = -lpthread

# 源文件路径
KVSERVER_DIR = ../../../kvserver
COMMON_DIR = ../../../common
SRC_DIR = .

# 需要的源文件
LSM_SOURCES = $(KVSERVER_DIR)/lsm_memtable.c
KV_SOURCES = $(KVSERVER_DIR)/kv_memory.c $(KVSERVER_DIR)/kv_error.c
TEST_SOURCES = lsm_memtable_test.c

# 目标文件
OBJECTS = lsm_memtable.o kv_memory.o kv_error.o lsm_memtable_test.o
TARGET = lsm_memtable_test

# 默认目标
all: $(TARGET)

# 编译目标程序
$(TARGET): $(OBJECTS)
	@echo "正在链接 $(TARGET)..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "✓ $(TARGET) 编译完成"

# 编译LSM MemTable源文件
lsm_memtable.o: $(LSM_SOURCES)
	@echo "正在编译 lsm_memtable.c..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $(KVSERVER_DIR)/lsm_memtable.c -o $@

# 编译内存管理源文件
kv_memory.o: $(KVSERVER_DIR)/kv_memory.c
	@echo "正在编译 kv_memory.c..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $(KVSERVER_DIR)/kv_memory.c -o $@

# 编译错误处理源文件
kv_error.o: $(KVSERVER_DIR)/kv_error.c
	@echo "正在编译 kv_error.c..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $(KVSERVER_DIR)/kv_error.c -o $@

# 编译测试源文件
lsm_memtable_test.o: $(TEST_SOURCES)
	@echo "正在编译 lsm_memtable_test.c..."
	$(CC) $(CFLAGS) $(INCLUDES) -c lsm_memtable_test.c -o $@

# 运行测试
test: $(TARGET)
	@echo "开始运行 LSM-Tree MemTable 测试..."
	@echo "================================================"
	./$(TARGET)
	@echo "================================================"

# 运行测试并显示详细信息
test-verbose: $(TARGET)
	@echo "开始运行 LSM-Tree MemTable 详细测试..."
	@echo "================================================"
	./$(TARGET) -v
	@echo "================================================"

# 运行内存泄漏检测
test-valgrind: $(TARGET)
	@echo "开始运行内存泄漏检测..."
	@echo "================================================"
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)
	@echo "================================================"

# 运行性能测试
test-perf: $(TARGET)
	@echo "开始运行性能测试..."
	@echo "================================================"
	time ./$(TARGET)
	@echo "================================================"

# 清理生成的文件
clean:
	@echo "正在清理生成的文件..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "✓ 清理完成"

# 重新编译
rebuild: clean all

# 检查源文件是否存在
check-sources:
	@echo "检查源文件是否存在..."
	@test -f $(KVSERVER_DIR)/lsm_memtable.h || (echo "❌ 未找到 lsm_memtable.h" && exit 1)
	@test -f $(KVSERVER_DIR)/lsm_memtable.c || (echo "❌ 未找到 lsm_memtable.c" && exit 1)
	@test -f $(KVSERVER_DIR)/kv_memory.c || (echo "❌ 未找到 kv_memory.c" && exit 1)
	@test -f $(KVSERVER_DIR)/kv_error.c || (echo "❌ 未找到 kv_error.c" && exit 1)
	@test -f lsm_memtable_test.c || (echo "❌ 未找到 lsm_memtable_test.c" && exit 1)
	@echo "✓ 所有源文件都存在"

# 显示帮助信息
help:
	@echo "LSM-Tree MemTable 测试 Makefile"
	@echo "================================"
	@echo "可用的目标："
	@echo "  all           - 编译所有文件（默认）"
	@echo "  $(TARGET)     - 编译测试程序"
	@echo "  test          - 运行基础测试"
	@echo "  test-verbose  - 运行详细测试"
	@echo "  test-valgrind - 运行内存泄漏检测"
	@echo "  test-perf     - 运行性能测试"
	@echo "  clean         - 清理生成的文件"
	@echo "  rebuild       - 重新编译"
	@echo "  check-sources - 检查源文件是否存在"
	@echo "  help          - 显示此帮助信息"

# 安装依赖项（如果需要）
install-deps:
	@echo "检查依赖项..."
	@which gcc > /dev/null || (echo "❌ 需要安装 gcc" && exit 1)
	@which valgrind > /dev/null || echo "⚠️  建议安装 valgrind 以进行内存检测"
	@echo "✓ 依赖项检查完成"

.PHONY: all test test-verbose test-valgrind test-perf clean rebuild check-sources help install-deps 