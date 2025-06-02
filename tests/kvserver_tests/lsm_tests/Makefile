# LSM-Tree MemTable、SSTable和压缩机制测试Makefile
# @Author: Lzww0608  
# @Date: 2025-6-2 23:45:25
# @LastEditTime: 2025-6-2 23:45:32

CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -pthread -D_GNU_SOURCE
INCLUDES = -I../../../kvserver
LIBS = -lpthread -lm

# 源文件路径
KVSERVER_SRC = ../../../kvserver
TEST_SRC = .

# 目标文件
MEMTABLE_MANAGER_TEST = lsm_memtable_manager_test
MEMTABLE_TEST = lsm_memtable_test
SSTABLE_TEST = lsm_sstable_test
COMPACTION_TEST = lsm_compaction_test

# LSM相关源文件
LSM_SOURCES = $(KVSERVER_SRC)/lsm_memtable.c \
              $(KVSERVER_SRC)/lsm_memtable_manager.c \
              $(KVSERVER_SRC)/lsm_sstable.c \
              $(KVSERVER_SRC)/lsm_compaction.c \
              $(KVSERVER_SRC)/kv_memory.c \
              $(KVSERVER_SRC)/kv_error.c

# 测试源文件
TEST_SOURCES = lsm_memtable_manager_test.c lsm_sstable_test.c lsm_compaction_test.c

# 目标文件列表
LSM_OBJECTS = $(patsubst $(KVSERVER_SRC)/%.c, %.o, $(LSM_SOURCES))
TEST_OBJECTS = $(patsubst %.c, %.o, $(TEST_SOURCES))

# 默认目标
.PHONY: all clean test memtable_manager_test memtable_test sstable_test compaction_test help

all: $(MEMTABLE_MANAGER_TEST) $(MEMTABLE_TEST) $(SSTABLE_TEST) $(COMPACTION_TEST)

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

# 构建SSTable测试
$(SSTABLE_TEST): $(LSM_OBJECTS) lsm_sstable_test.o
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "✅ 构建完成: $@"

# 构建压缩机制测试
$(COMPACTION_TEST): $(LSM_OBJECTS) lsm_compaction_test.o
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "✅ 构建完成: $@"

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

# 运行SSTable测试
test_sstable: $(SSTABLE_TEST)
	@echo "🧪 运行LSM-Tree SSTable测试..."
	@echo "============================="
	./$(SSTABLE_TEST)

# 运行压缩机制测试
test_compaction: $(COMPACTION_TEST)
	@echo "🧪 运行LSM-Tree压缩机制测试..."
	@echo "=============================="
	./$(COMPACTION_TEST)

# 运行所有测试
test: test_manager test_memtable test_sstable test_compaction
	@echo ""
	@echo "🎉 LSM-Tree所有测试完成!"

# 内存检查
memcheck_manager: $(MEMTABLE_MANAGER_TEST)
	@echo "🔍 运行Valgrind内存检查（管理器）..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(MEMTABLE_MANAGER_TEST)

memcheck_sstable: $(SSTABLE_TEST)
	@echo "🔍 运行Valgrind内存检查（SSTable）..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(SSTABLE_TEST)

memcheck_compaction: $(COMPACTION_TEST)
	@echo "🔍 运行Valgrind内存检查（压缩机制）..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(COMPACTION_TEST)

memcheck: memcheck_manager memcheck_sstable memcheck_compaction
	@echo "🎉 所有内存检查完成!"

# 性能测试
perf_test: $(MEMTABLE_MANAGER_TEST) $(SSTABLE_TEST) $(COMPACTION_TEST)
	@echo "⚡ 运行性能测试..."
	@echo "=================="
	@echo "MemTable管理器性能:"
	time ./$(MEMTABLE_MANAGER_TEST)
	@echo ""
	@echo "SSTable性能:"
	time ./$(SSTABLE_TEST)
	@echo ""
	@echo "压缩机制性能:"
	time ./$(COMPACTION_TEST)

# 压力测试
stress_test: $(MEMTABLE_MANAGER_TEST) $(SSTABLE_TEST) $(COMPACTION_TEST)
	@echo "💪 运行压力测试（多次执行）..."
	@echo "=============================="
	@echo "管理器压力测试:"
	@for i in 1 2 3; do \
		echo "第$$i次测试:"; \
		./$(MEMTABLE_MANAGER_TEST) || exit 1; \
		echo ""; \
	done
	@echo "SSTable压力测试:"
	@for i in 1 2 3; do \
		echo "第$$i次测试:"; \
		./$(SSTABLE_TEST) || exit 1; \
		echo ""; \
	done
	@echo "压缩机制压力测试:"
	@for i in 1 2 3; do \
		echo "第$$i次测试:"; \
		./$(COMPACTION_TEST) || exit 1; \
		echo ""; \
	done
	@echo "✅ 压力测试全部通过!"

# 快速测试（只运行压缩机制）
quick_test: $(COMPACTION_TEST)
	@echo "🚀 快速压缩机制测试..."
	@echo "===================="
	./$(COMPACTION_TEST)

# 生产环境测试
production_test: $(SSTABLE_TEST) $(COMPACTION_TEST)
	@echo "🏭 生产环境LSM-Tree测试..."
	@echo "========================="
	@echo "运行全面的SSTable功能验证..."
	./$(SSTABLE_TEST)
	@echo ""
	@echo "运行全面的压缩机制验证..."
	./$(COMPACTION_TEST)
	@echo ""
	@echo "文件系统检查..."
	@ls -la /tmp/*_test* 2>/dev/null || echo "临时文件已清理"
	@echo "✅ 生产环境测试完成"

# 完整的LSM-Tree集成测试
integration_test: all
	@echo "🔗 运行LSM-Tree完整集成测试..."
	@echo "============================="
	@echo "1. MemTable管理器测试:"
	./$(MEMTABLE_MANAGER_TEST)
	@echo ""
	@echo "2. SSTable文件格式测试:"
	./$(SSTABLE_TEST)
	@echo ""
	@echo "3. 压缩机制测试:"
	./$(COMPACTION_TEST)
	@echo ""
	@echo "🎉 LSM-Tree完整集成测试通过!"

# 压缩机制专项测试
compaction_deep_test: $(COMPACTION_TEST)
	@echo "🔍 压缩机制深度测试..."
	@echo "===================="
	@echo "标准测试:"
	./$(COMPACTION_TEST)
	@echo ""
	@echo "内存检查测试:"
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./$(COMPACTION_TEST)
	@echo ""
	@echo "多次重复测试:"
	@for i in 1 2 3 4 5; do \
		echo "第$$i次测试:"; \
		./$(COMPACTION_TEST) || exit 1; \
	done
	@echo "✅ 压缩机制深度测试完成!"

# 清理
clean:
	@echo "🧹 清理编译文件..."
	rm -f *.o $(MEMTABLE_MANAGER_TEST) $(MEMTABLE_TEST) $(SSTABLE_TEST) $(COMPACTION_TEST)
	rm -f /tmp/test_*.sst /tmp/lsm_compaction_test*
	@echo "✅ 清理完成"

# 帮助信息
help:
	@echo "LSM-Tree MemTable、SSTable和压缩机制测试Makefile使用说明"
	@echo "======================================================="
	@echo ""
	@echo "可用目标:"
	@echo "  all                    - 构建所有测试程序"
	@echo "  $(MEMTABLE_MANAGER_TEST)  - 构建管理器测试程序"
	@echo "  $(MEMTABLE_TEST)       - 构建MemTable测试程序"
	@echo "  $(SSTABLE_TEST)        - 构建SSTable测试程序"
	@echo "  $(COMPACTION_TEST)     - 构建压缩机制测试程序"
	@echo ""
	@echo "测试目标:"
	@echo "  test                   - 运行所有测试"
	@echo "  test_manager           - 运行管理器测试"
	@echo "  test_memtable          - 运行MemTable测试"
	@echo "  test_sstable           - 运行SSTable测试"
	@echo "  test_compaction        - 运行压缩机制测试"
	@echo "  quick_test             - 快速压缩机制测试"
	@echo "  production_test        - 生产环境测试"
	@echo "  integration_test       - 完整集成测试"
	@echo "  compaction_deep_test   - 压缩机制深度测试"
	@echo ""
	@echo "调试目标:"
	@echo "  memcheck               - 运行所有Valgrind内存检查"
	@echo "  memcheck_manager       - 运行管理器内存检查"
	@echo "  memcheck_sstable       - 运行SSTable内存检查"
	@echo "  memcheck_compaction    - 运行压缩机制内存检查"
	@echo "  perf_test              - 运行性能测试"
	@echo "  stress_test            - 运行压力测试"
	@echo ""
	@echo "维护目标:"
	@echo "  clean                  - 清理编译文件"
	@echo "  help                   - 显示此帮助信息"
	@echo ""
	@echo "使用示例:"
	@echo "  make                       # 构建所有测试"
	@echo "  make test                  # 运行所有测试"
	@echo "  make test_compaction       # 只运行压缩机制测试"
	@echo "  make quick_test            # 快速压缩机制测试"
	@echo "  make integration_test      # 完整集成测试"
	@echo "  make compaction_deep_test  # 压缩机制深度测试"
	@echo "  make memcheck_compaction   # 压缩机制内存检查"
	@echo "  make clean                 # 清理"

# 检查依赖
check_deps:
	@echo "🔍 检查编译依赖..."
	@echo "=================="
	@echo "检查编译器:"
	@which $(CC) >/dev/null && echo "✅ $(CC) 可用" || echo "❌ $(CC) 不可用"
	@echo "检查pthread库:"
	@echo "#include <pthread.h>" | $(CC) -x c -c - -o /dev/null 2>/dev/null && echo "✅ pthread 可用" || echo "❌ pthread 不可用"
	@echo "检查源文件:"
	@for src in $(LSM_SOURCES); do \
		if [ -f "$$src" ]; then \
			echo "✅ $$src"; \
		else \
			echo "❌ $$src (缺失)"; \
		fi; \
	done
	@echo "检查测试文件:"
	@for test in $(TEST_SOURCES); do \
		if [ -f "$$test" ]; then \
			echo "✅ $$test"; \
		else \
			echo "❌ $$test (缺失)"; \
		fi; \
	done

# 显示版本信息
version:
	@echo "LSM-Tree测试套件版本信息"
	@echo "======================"
	@echo "版本: 3.3 (压缩机制)"
	@echo "日期: 2025-6-1"
	@echo "功能: MemTable + SSTable + 压缩机制"
	@echo "编译器: $(CC)"
	@echo "编译选项: $(CFLAGS)"
	@echo "链接库: $(LIBS)" 