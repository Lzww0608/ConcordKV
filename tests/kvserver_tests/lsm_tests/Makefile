# LSM-Tree MemTable测试Makefile
# @Author: Lzww0608  
# @Date: 2025-6-1 12:00:00

CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -pthread
INCLUDES = -I../../../kvserver
LIBS = -lpthread

# 源文件路径
KVSERVER_SRC = ../../../kvserver
TEST_SRC = .

# 目标文件
MEMTABLE_MANAGER_TEST = lsm_memtable_manager_test
MEMTABLE_TEST = lsm_memtable_test

# LSM相关源文件
LSM_SOURCES = $(KVSERVER_SRC)/lsm_memtable.c \
              $(KVSERVER_SRC)/lsm_memtable_manager.c \
              $(KVSERVER_SRC)/kv_memory.c \
              $(KVSERVER_SRC)/kv_error.c

# 测试源文件
TEST_SOURCES = lsm_memtable_manager_test.c

# 目标文件列表
LSM_OBJECTS = $(patsubst $(KVSERVER_SRC)/%.c, %.o, $(LSM_SOURCES))
TEST_OBJECTS = $(patsubst %.c, %.o, $(TEST_SOURCES))

# 默认目标
.PHONY: all clean test memtable_manager_test memtable_test help

all: $(MEMTABLE_MANAGER_TEST) $(MEMTABLE_TEST)

# 构建管理器测试
$(MEMTABLE_MANAGER_TEST): $(LSM_OBJECTS) lsm_memtable_manager_test.o
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "✅ 构建完成: $@"

# 构建MemTable测试（如果存在）
$(MEMTABLE_TEST): $(LSM_OBJECTS) lsm_memtable_test.o
	@if [ -f "lsm_memtable_test.c" ]; then \
		echo "🔗 链接 $@..."; \
		$(CC) $(CFLAGS) -o $@ $^ $(LIBS); \
		echo "✅ 构建完成: $@"; \
	else \
		echo "ℹ️  lsm_memtable_test.c 不存在，跳过构建"; \
	fi

# 编译规则
%.o: $(KVSERVER_SRC)/%.c
	@echo "🔨 编译 $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.c
	@echo "🔨 编译 $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 运行管理器测试
test_manager: $(MEMTABLE_MANAGER_TEST)
	@echo "🧪 运行LSM-Tree MemTable管理器测试..."
	@echo "=================================="
	./$(MEMTABLE_MANAGER_TEST)

# 运行MemTable测试
test_memtable: $(MEMTABLE_TEST)
	@if [ -f "$(MEMTABLE_TEST)" ]; then \
		echo "🧪 运行LSM-Tree MemTable测试..."; \
		echo "=============================="; \
		./$(MEMTABLE_TEST); \
	else \
		echo "ℹ️  MemTable测试程序不存在"; \
	fi

# 运行所有测试
test: test_manager test_memtable
	@echo ""
	@echo "🎉 LSM-Tree测试完成!"

# 内存检查
memcheck_manager: $(MEMTABLE_MANAGER_TEST)
	@echo "🔍 运行Valgrind内存检查..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(MEMTABLE_MANAGER_TEST)

# 性能测试
perf_test: $(MEMTABLE_MANAGER_TEST)
	@echo "⚡ 运行性能测试..."
	@echo "=================="
	time ./$(MEMTABLE_MANAGER_TEST)

# 压力测试
stress_test: $(MEMTABLE_MANAGER_TEST)
	@echo "💪 运行压力测试（多次执行）..."
	@echo "=============================="
	@for i in 1 2 3 4 5; do \
		echo "第$$i次测试:"; \
		./$(MEMTABLE_MANAGER_TEST) || exit 1; \
		echo ""; \
	done
	@echo "✅ 压力测试全部通过!"

# 清理
clean:
	@echo "🧹 清理编译文件..."
	rm -f *.o $(MEMTABLE_MANAGER_TEST) $(MEMTABLE_TEST)
	@echo "✅ 清理完成"

# 帮助信息
help:
	@echo "LSM-Tree MemTable测试Makefile使用说明"
	@echo "======================================"
	@echo ""
	@echo "可用目标:"
	@echo "  all                    - 构建所有测试程序"
	@echo "  $(MEMTABLE_MANAGER_TEST)  - 构建管理器测试程序"
	@echo "  $(MEMTABLE_TEST)       - 构建MemTable测试程序"
	@echo ""
	@echo "测试目标:"
	@echo "  test                   - 运行所有测试"
	@echo "  test_manager           - 运行管理器测试"
	@echo "  test_memtable          - 运行MemTable测试"
	@echo ""
	@echo "调试目标:"
	@echo "  memcheck_manager       - 运行Valgrind内存检查"
	@echo "  perf_test              - 运行性能测试"
	@echo "  stress_test            - 运行压力测试"
	@echo ""
	@echo "维护目标:"
	@echo "  clean                  - 清理编译文件"
	@echo "  help                   - 显示此帮助信息"
	@echo ""
	@echo "使用示例:"
	@echo "  make                   # 构建所有测试"
	@echo "  make test              # 运行所有测试"
	@echo "  make test_manager      # 只运行管理器测试"
	@echo "  make memcheck_manager  # 内存检查"
	@echo "  make clean             # 清理"

# 检查依赖
check_deps:
	@echo "🔍 检查依赖文件..."
	@echo "LSM源文件检查:"
	@for file in $(LSM_SOURCES); do \
		if [ -f "$$file" ]; then \
			echo "  ✅ $$file"; \
		else \
			echo "  ❌ $$file (缺失)"; \
		fi \
	done
	@echo ""
	@echo "测试文件检查:"
	@for file in $(TEST_SOURCES); do \
		if [ -f "$$file" ]; then \
			echo "  ✅ $$file"; \
		else \
			echo "  ❌ $$file (缺失)"; \
		fi \
	done

# 显示编译信息
info:
	@echo "LSM-Tree测试编译信息"
	@echo "==================="
	@echo "编译器: $(CC)"
	@echo "编译选项: $(CFLAGS)"
	@echo "包含路径: $(INCLUDES)"
	@echo "链接库: $(LIBS)"
	@echo ""
	@echo "LSM源文件:"
	@for file in $(LSM_SOURCES); do echo "  $$file"; done
	@echo ""
	@echo "测试文件:"
	@for file in $(TEST_SOURCES); do echo "  $$file"; done

.SUFFIXES: .c .o 