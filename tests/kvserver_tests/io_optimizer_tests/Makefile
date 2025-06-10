# ConcordKV I/O 优化器测试 Makefile
# @Author: Lzww0608
# @Date: 2025-6-9 20:00:00

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2 -pthread
INCLUDES = -I../../../kvserver -I../../../common
LIBS = -lpthread -lrt -lm -luuid

# 源文件目录
KVSERVER_DIR = ../../../kvserver
COMMON_DIR = ../../../common

# 源文件
SOURCES = kv_io_optimizer_test.c \
          $(KVSERVER_DIR)/kv_io_optimizer.c \
          $(KVSERVER_DIR)/kv_memory.c \
          $(COMMON_DIR)/util/util.c

# 目标文件
OBJECTS = $(SOURCES:.c=.o)

# 测试可执行文件
TARGET = kv_io_optimizer_test

# 默认目标
.PHONY: all clean test run

all: $(TARGET)

# 编译可执行文件
$(TARGET): $(OBJECTS)
	@echo "🔗 链接 $(TARGET)..."
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET) $(LIBS)
	@echo "✅ 编译完成: $(TARGET)"

# 编译源文件
%.o: %.c
	@echo "🔨 编译 $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 运行测试
test: $(TARGET)
	@echo "🚀 运行 I/O 优化器测试..."
	@echo "======================================"
	./$(TARGET)
	@echo "======================================"

# 快速运行
run: test

# 调试运行
debug: $(TARGET)
	@echo "🐛 调试模式运行测试..."
	gdb -ex run -ex bt -ex quit --args ./$(TARGET)

# 内存检查
valgrind: $(TARGET)
	@echo "🔍 内存检查模式运行..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

# 性能分析
perf: $(TARGET)
	@echo "📊 性能分析模式运行..."
	perf record -g ./$(TARGET)
	perf report

# 清理
clean:
	@echo "🧹 清理编译文件..."
	rm -f $(OBJECTS) $(TARGET)
	rm -f /tmp/kv_io_test_*.tmp
	rm -f perf.data*
	@echo "✅ 清理完成"

# 安装依赖（如果需要）
install-deps:
	@echo "📦 检查依赖..."
	@which gcc >/dev/null || (echo "❌ 需要安装 gcc"; exit 1)
	@which valgrind >/dev/null || echo "⚠️  建议安装 valgrind 用于内存检查"
	@which perf >/dev/null || echo "⚠️  建议安装 perf 用于性能分析"
	@echo "✅ 依赖检查完成"

# 帮助信息
help:
	@echo "🔧 ConcordKV I/O 优化器测试 Makefile"
	@echo ""
	@echo "可用目标:"
	@echo "  all       - 编译测试程序"
	@echo "  test      - 编译并运行测试"
	@echo "  run       - 快速运行测试"
	@echo "  debug     - 调试模式运行"
	@echo "  valgrind  - 内存检查模式运行"
	@echo "  perf      - 性能分析模式运行"
	@echo "  clean     - 清理编译文件"
	@echo "  help      - 显示此帮助信息"
	@echo ""
	@echo "示例:"
	@echo "  make test          # 编译并运行测试"
	@echo "  make clean test    # 清理后重新编译运行"
	@echo "  make valgrind      # 内存检查运行"

# 依赖关系
kv_io_optimizer_test.o: kv_io_optimizer_test.c $(KVSERVER_DIR)/kv_io_optimizer.h $(KVSERVER_DIR)/kv_error.h
$(KVSERVER_DIR)/kv_io_optimizer.o: $(KVSERVER_DIR)/kv_io_optimizer.c $(KVSERVER_DIR)/kv_io_optimizer.h
$(KVSERVER_DIR)/kv_memory.o: $(KVSERVER_DIR)/kv_memory.c $(KVSERVER_DIR)/kv_memory.h
$(COMMON_DIR)/util/util.o: $(COMMON_DIR)/util/util.c $(COMMON_DIR)/util/util.h 