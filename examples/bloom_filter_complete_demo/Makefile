# ConcordKV 完整布隆过滤器演示程序 Makefile
# Author: Lzww0608
# Date: 2025-6-6 18:45:00

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -g
INCLUDES = -I../../kvserver -I../../
LIBS = -lm -lpthread -lrt

# 源文件
DEMO_SRC = bloom_filter_complete_demo.c
BLOOM_SRC = ../../kvserver/lsm_bloom_filter_optimized.c
SOURCES = $(DEMO_SRC) $(BLOOM_SRC)

# 目标文件
TARGET = bloom_filter_complete_demo
OBJECTS = $(SOURCES:.c=.o)

# 默认目标
all: $(TARGET)

# 编译演示程序
$(TARGET): $(OBJECTS)
	@echo "🔧 链接布隆过滤器完整演示程序..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "✅ 编译完成: $@"

# 编译源文件
%.o: %.c
	@echo "🔨 编译 $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 运行演示程序
run: $(TARGET)
	@echo "🚀 运行布隆过滤器完整演示..."
	./$(TARGET)

# 性能测试
benchmark: $(TARGET)
	@echo "⚡ 运行性能基准测试..."
	./$(TARGET) | grep -A 20 "性能对比测试"

# 内存检查
valgrind: $(TARGET)
	@echo "🔍 内存泄漏检查..."
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

# 调试模式编译
debug: CFLAGS += -DDEBUG -O0 -g3
debug: clean $(TARGET)
	@echo "🐛 调试版本编译完成"

# 发布模式编译
release: CFLAGS += -DNDEBUG -O3 -march=native -flto
release: clean $(TARGET)
	@echo "🚀 发布版本编译完成"

# 分析编译器优化
analyze: CFLAGS += -fopt-info-vec -fopt-info-loop
analyze: clean $(TARGET)
	@echo "📊 编译器优化分析完成"

# 代码覆盖率
coverage: CFLAGS += --coverage
coverage: LIBS += --coverage
coverage: clean $(TARGET)
	@echo "📈 覆盖率版本编译完成"
	./$(TARGET)
	gcov $(SOURCES)
	@echo "📊 覆盖率报告生成完成"

# 静态分析
static-analysis:
	@echo "🔎 运行静态代码分析..."
	cppcheck --enable=all --std=c99 $(SOURCES)
	@echo "✅ 静态分析完成"

# 格式化代码
format:
	@echo "🎨 格式化代码..."
	clang-format -i $(SOURCES) *.h
	@echo "✅ 代码格式化完成"

# 清理
clean:
	@echo "🧹 清理编译文件..."
	rm -f $(TARGET) $(OBJECTS) *.gcov *.gcda *.gcno
	@echo "✅ 清理完成"

# 帮助信息
help:
	@echo "🎯 ConcordKV 布隆过滤器完整演示程序"
	@echo "========================================="
	@echo "可用目标:"
	@echo "  all           - 编译演示程序 (默认)"
	@echo "  run           - 运行演示程序"
	@echo "  benchmark     - 运行性能基准测试"
	@echo "  valgrind      - 内存泄漏检查"
	@echo "  debug         - 编译调试版本"
	@echo "  release       - 编译发布版本"
	@echo "  analyze       - 编译器优化分析"
	@echo "  coverage      - 代码覆盖率分析"
	@echo "  static-analysis - 静态代码分析"
	@echo "  format        - 格式化代码"
	@echo "  clean         - 清理编译文件"
	@echo "  help          - 显示此帮助信息"

# 安装依赖 (Ubuntu/Debian)
install-deps:
	@echo "📦 安装编译依赖..."
	sudo apt-get update
	sudo apt-get install -y gcc make valgrind cppcheck clang-format gcov
	@echo "✅ 依赖安装完成"

# 创建演示数据
demo-data:
	@echo "📊 创建演示数据文件..."
	mkdir -p data
	echo "ConcordKV 布隆过滤器测试数据" > data/test_data.txt
	for i in {1..10000}; do echo "test_key_$$i" >> data/test_data.txt; done
	@echo "✅ 演示数据创建完成: data/test_data.txt"

# 系统信息
system-info:
	@echo "💻 系统信息:"
	@echo "  操作系统: $$(uname -s)"
	@echo "  架构: $$(uname -m)"
	@echo "  内核版本: $$(uname -r)"
	@echo "  编译器版本: $$($(CC) --version | head -1)"
	@echo "  CPU信息: $$(lscpu | grep 'Model name' | cut -d':' -f2 | xargs)"
	@echo "  内存信息: $$(free -h | grep 'Mem:' | awk '{print $$2}')"

.PHONY: all run benchmark valgrind debug release analyze coverage static-analysis format clean help install-deps demo-data system-info 