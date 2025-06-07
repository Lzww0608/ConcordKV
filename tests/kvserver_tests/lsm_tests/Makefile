# LSM-Tree完整测试套件Makefile
# @Author: Lzww0608  
# @Date: 2025-6-3 15:30:00
# @LastEditTime: 2025-6-3 22:23:12

# 编译器设置
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE -g -O2
INCLUDES = -I../../../kvserver
LDFLAGS = -lpthread -lz -lm

# 源文件路径
KVSERVER_SRC = ../../../kvserver

# LSM相关源文件
LSM_SOURCES = $(KVSERVER_SRC)/lsm_memtable.c \
              $(KVSERVER_SRC)/lsm_memtable_manager.c \
              $(KVSERVER_SRC)/lsm_sstable.c \
              $(KVSERVER_SRC)/lsm_compaction.c \
              $(KVSERVER_SRC)/lsm_tree.c \
              $(KVSERVER_SRC)/kv_memory.c \
              $(KVSERVER_SRC)/kv_error.c

# 目标文件
LSM_OBJECTS = lsm_memtable.o lsm_memtable_manager.o lsm_sstable.o lsm_compaction.o lsm_tree.o kv_memory.o kv_error.o

# 测试程序
TESTS = lsm_memtable_test lsm_sstable_test lsm_memtable_manager_test lsm_compaction_test lsm_tree_test lsm_multithread_compaction_test lsm_multithread_performance_test lsm_batch_write_test

# 核心测试（不包括集成测试）
CORE_TESTS = lsm_memtable_test lsm_sstable_test lsm_memtable_manager_test lsm_compaction_test

# 多线程测试
MULTITHREAD_TESTS = lsm_multithread_compaction_test lsm_multithread_performance_test

# 集成测试
INTEGRATION_TESTS = lsm_tree_test

# 批量写入测试
BATCH_TESTS = lsm_batch_write_test

# 默认目标
.PHONY: all clean test test-core test-integration test-all memcheck help

all: $(TESTS)

# 编译目标文件规则
%.o: $(KVSERVER_SRC)/%.c
	@echo "🔨 编译 $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 编译测试文件规则
%.o: %.c
	@echo "🔨 编译 $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# MemTable测试
lsm_memtable_test: lsm_memtable_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) lsm_memtable_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# MemTable管理器测试
lsm_memtable_manager_test: lsm_memtable_manager_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) lsm_memtable_manager_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# SSTable测试
lsm_sstable_test: lsm_sstable_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) lsm_sstable_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# 压缩机制测试
lsm_compaction_test: lsm_compaction_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) lsm_compaction_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# LSM-Tree完整集成测试
lsm_tree_test: lsm_tree_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) lsm_tree_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# 多线程压缩测试
lsm_multithread_compaction_test: lsm_multithread_compaction_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) lsm_multithread_compaction_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# 多线程压缩性能测试
lsm_multithread_performance_test: lsm_multithread_performance_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) lsm_multithread_performance_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# 批量写入测试
lsm_batch_write_test: lsm_batch_write_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) lsm_batch_write_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# 调试批量写入测试
debug_batch_test: debug_batch_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) debug_batch_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# 调试去重功能测试
debug_dedup_test: debug_dedup_test.c $(LSM_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(CFLAGS) $(INCLUDES) debug_dedup_test.c $(LSM_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 构建完成: $@"

# === 测试运行目标 ===

# 运行核心组件测试
test-core: $(CORE_TESTS)
	@echo "🚀 运行LSM-Tree核心组件测试..."
	@echo "=================================="
	@for test in $(CORE_TESTS); do \
		echo ""; \
		echo "🧪 运行 $$test..."; \
		echo "----------------------------------------"; \
		./$$test || exit 1; \
	done
	@echo ""
	@echo "🎉 核心组件测试全部通过！"

# 运行多线程测试
test-multithread: $(MULTITHREAD_TESTS)
	@echo "🚀 运行LSM-Tree多线程压缩测试..."
	@echo "=================================="
	@for test in $(MULTITHREAD_TESTS); do \
		echo ""; \
		echo "🧪 运行 $$test..."; \
		echo "----------------------------------------"; \
		./$$test || exit 1; \
	done
	@echo ""
	@echo "🎉 多线程测试全部通过！"

# 运行集成测试
test-integration: $(INTEGRATION_TESTS)
	@echo "🚀 运行LSM-Tree完整集成测试..."
	@echo "================================"
	@for test in $(INTEGRATION_TESTS); do \
		echo ""; \
		echo "🧪 运行 $$test..."; \
		echo "----------------------------------------"; \
		./$$test || exit 1; \
	done
	@echo ""
	@echo "🎉 集成测试全部通过！"

# 运行批量写入测试
test-batch: $(BATCH_TESTS)
	@echo "🚀 运行LSM-Tree批量写入功能测试..."
	@echo "=================================="
	@for test in $(BATCH_TESTS); do \
		echo ""; \
		echo "🧪 运行 $$test..."; \
		echo "----------------------------------------"; \
		./$$test || exit 1; \
	done
	@echo ""
	@echo "🎉 批量写入测试全部通过！"

# 运行所有测试
test-all: $(TESTS)
	@echo "🚀 运行LSM-Tree完整测试套件..."
	@echo "==============================="
	@echo "包含测试："
	@for test in $(TESTS); do echo "  - $$test"; done
	@echo ""
	@for test in $(TESTS); do \
		echo "🧪 运行 $$test..."; \
		echo "----------------------------------------"; \
		./$$test || exit 1; \
		echo ""; \
	done
	@echo "🎉 所有测试全部通过！"

# 保持向后兼容的test目标
test: test-core

# === 质量检查目标 ===

# 内存检查
memcheck: $(TESTS)
	@echo "🔍 运行Valgrind内存检查..."
	@echo "=========================="
	@for test in $(TESTS); do \
		echo ""; \
		echo "🔍 内存检查: $$test"; \
		echo "----------------------------------------"; \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$$test || exit 1; \
	done
	@echo ""
	@echo "🎉 内存检查全部通过！"

# 性能基准测试
perf: $(TESTS)
	@echo "⚡ 运行性能基准测试..."
	@echo "====================="
	@for test in $(TESTS); do \
		echo ""; \
		echo "⚡ 性能测试: $$test"; \
		echo "----------------------------------------"; \
		time ./$$test; \
	done
	@echo ""
	@echo "🎉 性能测试完成！"

# 压力测试（多次运行）
stress: $(TESTS)
	@echo "💪 运行压力测试（每个测试3次）..."
	@echo "================================="
	@for test in $(TESTS); do \
		echo ""; \
		echo "💪 压力测试: $$test"; \
		echo "----------------------------------------"; \
		for i in 1 2 3; do \
			echo "第$$i次运行:"; \
			./$$test || exit 1; \
		done; \
	done
	@echo ""
	@echo "🎉 压力测试全部通过！"

# === 维护目标 ===

# 清理
clean:
	@echo "🧹 清理构建文件和测试数据..."
	rm -f $(LSM_OBJECTS) $(TESTS)
	rm -rf test_lsm_* ./lsm_data ./test_* *.log *.tmp
	@echo "✅ 清理完成"

# 深度清理（包括编辑器临时文件等）
distclean: clean
	@echo "🧹 深度清理..."
	find . -name "*~" -delete
	find . -name "*.swp" -delete
	find . -name "*.bak" -delete
	find . -name "core" -delete
	@echo "✅ 深度清理完成"

# 检查源文件是否存在
check-sources:
	@echo "🔍 检查源文件完整性..."
	@for src in $(LSM_SOURCES); do \
		if [ ! -f "$$src" ]; then \
			echo "❌ 缺少源文件: $$src"; \
			exit 1; \
		else \
			echo "✅ 找到: $$src"; \
		fi; \
	done
	@echo "🎉 所有源文件完整！"

# 构建状态检查
status:
	@echo "📊 构建状态："
	@echo "============="
	@echo "已构建的测试程序："
	@for test in $(TESTS); do \
		if [ -f "$$test" ]; then \
			echo "  ✅ $$test"; \
		else \
			echo "  ❌ $$test (未构建)"; \
		fi; \
	done
	@echo ""
	@echo "目标文件："
	@for obj in $(LSM_OBJECTS); do \
		if [ -f "$$obj" ]; then \
			echo "  ✅ $$obj"; \
		else \
			echo "  ❌ $$obj (未编译)"; \
		fi; \
	done

# 帮助信息
help:
	@echo "LSM-Tree完整测试套件 Makefile"
	@echo "=============================="
	@echo ""
	@echo "🎯 主要目标："
	@echo "  all              - 编译所有测试程序"
	@echo "  test-core        - 运行核心组件测试"
	@echo "  test-integration - 运行完整集成测试"
	@echo "  test-all         - 运行所有测试"
	@echo "  test             - 运行核心测试（向后兼容）"
	@echo ""
	@echo "🔍 质量检查："
	@echo "  memcheck         - Valgrind内存检查"
	@echo "  perf             - 性能基准测试"
	@echo "  stress           - 压力测试（多次运行）"
	@echo ""
	@echo "🧹 维护目标："
	@echo "  clean            - 清理构建文件"
	@echo "  distclean        - 深度清理"
	@echo "  check-sources    - 检查源文件完整性"
	@echo "  status           - 显示构建状态"
	@echo "  help             - 显示此帮助信息"
	@echo ""
	@echo "📋 测试程序列表："
	@echo "  核心测试: $(CORE_TESTS)"
	@echo "  集成测试: $(INTEGRATION_TESTS)"
	@echo ""
	@echo "💡 使用示例："
	@echo "  make all && make test-all    # 编译并运行所有测试"
	@echo "  make test-integration        # 只运行集成测试"
	@echo "  make memcheck               # 内存检查"

# 包含依赖关系（如果存在）
-include *.d 