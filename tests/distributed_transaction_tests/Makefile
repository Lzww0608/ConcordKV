#
# @Author: Lzww0608
# @Date: 2025-5-30 10:34:36	
# @LastEditors: Lzww0608
# @LastEditTime: 2025-5-30 11:17:38
# @Description: ConcordKV分布式事务测试Makefile
#

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
DEBUG_FLAGS = -g -O0 -DDEBUG
RELEASE_FLAGS = -O2 -DNDEBUG
PTHREAD_FLAGS = -pthread
MEMCHECK_FLAGS = -fsanitize=address -fno-omit-frame-pointer

# 目录设置
KVSERVER_DIR = ../../kvserver
TEST_DIR = .
BUILD_DIR = build
BIN_DIR = bin

# 源文件
KVSERVER_SOURCES = \
    $(KVSERVER_DIR)/kv_transaction.c \
    $(KVSERVER_DIR)/kv_distributed_transaction.c \
    $(KVSERVER_DIR)/kv_concurrency.c \
    $(KVSERVER_DIR)/kvstore_rbtree.c \
    $(KVSERVER_DIR)/rbtree_adapter.c

TEST_SOURCES = \
    test_distributed_transaction.c \
    test_priority_queue.c \
    test_2pc_protocol.c \
    test_timeout_handling.c

# 头文件路径
INCLUDES = -I$(KVSERVER_DIR) -I../common

# 库文件
LIBS = -lm -lrt

# 目标文件
TEST_TARGETS = \
    $(BIN_DIR)/test_distributed_transaction \
    $(BIN_DIR)/test_priority_queue \
    $(BIN_DIR)/test_2pc_protocol \
    $(BIN_DIR)/test_timeout_handling

# 默认目标
all: debug

# 调试版本
debug: CFLAGS += $(DEBUG_FLAGS)
debug: create_dirs $(TEST_TARGETS)

# 发布版本
release: CFLAGS += $(RELEASE_FLAGS)
release: create_dirs $(TEST_TARGETS)

# 内存检查版本
memcheck: CFLAGS += $(DEBUG_FLAGS) $(MEMCHECK_FLAGS)
memcheck: create_dirs $(TEST_TARGETS)

# 创建目录
create_dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# 编译分布式事务测试
$(BIN_DIR)/test_distributed_transaction: test_distributed_transaction.c $(KVSERVER_SOURCES)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# 编译优先级队列测试
$(BIN_DIR)/test_priority_queue: test_priority_queue.c $(KVSERVER_SOURCES)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# 编译2PC协议测试
$(BIN_DIR)/test_2pc_protocol: test_2pc_protocol.c $(KVSERVER_SOURCES)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# 编译超时处理测试
$(BIN_DIR)/test_timeout_handling: test_timeout_handling.c $(KVSERVER_SOURCES)
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# 运行所有测试
test: debug
	@echo "Running distributed transaction tests..."
	@./$(BIN_DIR)/test_distributed_transaction
	@./$(BIN_DIR)/test_priority_queue
	@./$(BIN_DIR)/test_2pc_protocol
	@./$(BIN_DIR)/test_timeout_handling

# 运行单个测试
test-distributed: debug
	./$(BIN_DIR)/test_distributed_transaction

test-priority: debug
	./$(BIN_DIR)/test_priority_queue

test-2pc: debug
	./$(BIN_DIR)/test_2pc_protocol

test-timeout: debug
	./$(BIN_DIR)/test_timeout_handling

# 使用Valgrind检查内存泄漏
valgrind: debug
	@echo "Running tests with Valgrind..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		./$(BIN_DIR)/test_distributed_transaction
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		./$(BIN_DIR)/test_priority_queue

# 生成测试报告
report: test
	@echo "Generating test report..."
	@echo "# Distributed Transaction Test Report" > test_report.md
	@echo "Generated on: $(shell date)" >> test_report.md
	@echo "" >> test_report.md
	@echo "## Test Results" >> test_report.md
	@./$(BIN_DIR)/test_distributed_transaction --report >> test_report.md 2>&1 || true
	@./$(BIN_DIR)/test_priority_queue --report >> test_report.md 2>&1 || true
	@./$(BIN_DIR)/test_2pc_protocol --report >> test_report.md 2>&1 || true
	@./$(BIN_DIR)/test_timeout_handling --report >> test_report.md 2>&1 || true

# 清理
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) test_report.md *.log *.out

# 清理并重新构建
rebuild: clean all

# 显示帮助
help:
	@echo "Available targets:"
	@echo "  all          - Build debug version (default)"
	@echo "  debug        - Build debug version with debug symbols"
	@echo "  release      - Build release version with optimizations"
	@echo "  memcheck     - Build with address sanitizer"
	@echo "  test         - Run all tests"
	@echo "  test-*       - Run specific test (distributed, priority, 2pc, timeout)"
	@echo "  valgrind     - Run tests with Valgrind memory checking"
	@echo "  report       - Generate test report"
	@echo "  clean        - Clean build files"
	@echo "  rebuild      - Clean and rebuild"
	@echo "  help         - Show this help message"

.PHONY: all debug release memcheck create_dirs test test-distributed test-priority test-2pc test-timeout valgrind report clean rebuild help 