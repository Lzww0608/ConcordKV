# Makefile for Range Query Tests
# ConcordKV Range Query and Prefix Scanning Tests

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2 -fPIC
LDFLAGS = -pthread -lm

# 目录路径
KVSERVER_DIR = ../../../kvserver
SRC_DIR = .
BUILD_DIR = build

# 源文件
RANGE_QUERY_SOURCES = \
	$(KVSERVER_DIR)/kv_range_query.c \
	$(KVSERVER_DIR)/kv_memory.c \
	$(KVSERVER_DIR)/kv_error.c \
	$(KVSERVER_DIR)/kvstore_btree.c \
	$(KVSERVER_DIR)/btree_adapter.c

TEST_SOURCES = test_range_query.c

# 目标文件
RANGE_QUERY_OBJECTS = $(patsubst $(KVSERVER_DIR)/%.c,$(BUILD_DIR)/%.o,$(RANGE_QUERY_SOURCES))
TEST_OBJECTS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(TEST_SOURCES))

# 可执行文件
TEST_EXECUTABLE = $(BUILD_DIR)/test_range_query

# 包含目录
INCLUDES = -I$(KVSERVER_DIR) -I../../../common

# 默认目标
all: $(BUILD_DIR) $(TEST_EXECUTABLE)

# 创建构建目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 编译范围查询源文件
$(BUILD_DIR)/%.o: $(KVSERVER_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 编译测试源文件
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 链接测试可执行文件
$(TEST_EXECUTABLE): $(RANGE_QUERY_OBJECTS) $(TEST_OBJECTS)
	$(CC) $(RANGE_QUERY_OBJECTS) $(TEST_OBJECTS) -o $@ $(LDFLAGS)

# 运行测试
test: $(TEST_EXECUTABLE)
	@echo "运行范围查询测试..."
	@timeout 30 ./$(TEST_EXECUTABLE) || (echo "测试超时或失败"; exit 1)

# 运行测试 (详细模式)
test-verbose: $(TEST_EXECUTABLE)
	@echo "运行范围查询测试 (详细模式)..."
	@timeout 30 ./$(TEST_EXECUTABLE) -v || (echo "测试超时或失败"; exit 1)

# 内存检查
valgrind: $(TEST_EXECUTABLE)
	@echo "使用Valgrind检查内存..."
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--suppressions=/usr/share/gdb/auto-load/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.30-gdb.py \
		./$(TEST_EXECUTABLE) 2>&1 | tee valgrind.log

# 清理
clean:
	rm -rf $(BUILD_DIR)
	rm -f *.log core.*

# 安装测试
install: $(TEST_EXECUTABLE)
	mkdir -p ../../../bin/tests
	cp $(TEST_EXECUTABLE) ../../../bin/tests/

# 帮助信息
help:
	@echo "可用目标:"
	@echo "  all          - 编译所有测试"
	@echo "  test         - 运行测试"
	@echo "  test-verbose - 运行测试(详细模式)"
	@echo "  valgrind     - 使用Valgrind检查内存"
	@echo "  clean        - 清理构建文件"
	@echo "  install      - 安装测试到bin目录"
	@echo "  help         - 显示此帮助信息"

.PHONY: all test test-verbose valgrind clean install help 