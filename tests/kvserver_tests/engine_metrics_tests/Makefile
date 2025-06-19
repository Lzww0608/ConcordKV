# ConcordKV 存储引擎监控系统集成测试 Makefile
# Author: Lzww0608

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pthread -g -O2
LDFLAGS = -pthread

# 测试目标
TEST_TARGET = test_metrics_integration
TEST_SRC = test_metrics_integration.c

# 默认目标
all: $(TEST_TARGET)

# 编译集成测试
$(TEST_TARGET): $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# 运行测试
test: $(TEST_TARGET)
	@echo "=== 运行监控系统集成测试 ==="
	./$(TEST_TARGET)

# 运行测试并输出详细信息
test-verbose: $(TEST_TARGET)
	@echo "=== 运行监控系统集成测试 (详细模式) ==="
	./$(TEST_TARGET) -v

# 内存检查
valgrind: $(TEST_TARGET)
	@echo "=== 使用 Valgrind 进行内存检查 ==="
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TEST_TARGET)

# 性能分析
perf: $(TEST_TARGET)
	@echo "=== 使用 perf 进行性能分析 ==="
	perf record -g ./$(TEST_TARGET)
	perf report

# 清理
clean:
	rm -f $(TEST_TARGET)
	rm -f perf.data*
	rm -f core.*

# 安装依赖
install-deps:
	@echo "=== 安装测试依赖 ==="
	sudo apt-get update
	sudo apt-get install -y build-essential valgrind linux-perf

# 代码格式化
format:
	@echo "=== 格式化代码 ==="
	clang-format -i $(TEST_SRC)

# 静态分析
static-analysis:
	@echo "=== 运行静态分析 ==="
	cppcheck --enable=all --std=c11 $(TEST_SRC)

# 帮助信息
help:
	@echo "ConcordKV 监控系统集成测试 Makefile"
	@echo ""
	@echo "可用目标:"
	@echo "  all              - 编译所有测试"
	@echo "  test             - 运行集成测试"
	@echo "  test-verbose     - 运行详细模式测试"
	@echo "  valgrind         - 使用 Valgrind 进行内存检查"
	@echo "  perf             - 使用 perf 进行性能分析"
	@echo "  clean            - 清理生成的文件"
	@echo "  install-deps     - 安装测试依赖"
	@echo "  format           - 格式化代码"
	@echo "  static-analysis  - 运行静态分析"
	@echo "  help             - 显示此帮助信息"

.PHONY: all test test-verbose valgrind perf clean install-deps format static-analysis help 