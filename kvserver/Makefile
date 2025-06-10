# 编译器和选项
CC = gcc
CFLAGS = -Wall -g -O2 -fPIC -std=c99
LDFLAGS = -lpthread

# 源文件和目标文件（移除有问题的文件）
CORE_OBJS = kvstore.o kvstore_array.o kvstore_hash.o kvstore_rbtree.o rbtree_adapter.o \
            kv_persist.o kv_concurrency.o kv_transaction.o kv_error.o kv_cluster.o \
            epoll_entry.o

# 测试程序使用的对象文件（不包含main函数的kvstore.o）
TEST_OBJS = kvstore_array.o kvstore_hash.o kvstore_rbtree.o rbtree_adapter.o \
            kv_persist.o kv_concurrency.o kv_transaction.o kv_error.o kv_cluster.o \
            kv_memory.o

# 新增的统一引擎接口相关文件
ENGINE_OBJS = kv_engine_factory.o

# B+Tree存储引擎文件
BTREE_OBJS = kvstore_btree.o btree_adapter.o

# LSM-Tree相关文件 🔥
LSM_OBJS = lsm_memtable.o lsm_memtable_manager.o

# Cache缓存层相关文件 ⚡
CACHE_OBJS = kv_cache.o

# I/O优化器相关文件 🚀
IO_OPTIMIZER_OBJS = kv_io_optimizer.o

# 所有目标文件
ALL_OBJS = $(CORE_OBJS) $(ENGINE_OBJS) $(BTREE_OBJS) $(LSM_OBJS) $(CACHE_OBJS) $(IO_OPTIMIZER_OBJS)

# 测试程序使用的所有对象文件
ALL_TEST_OBJS = $(TEST_OBJS) $(ENGINE_OBJS) $(BTREE_OBJS) $(LSM_OBJS) $(CACHE_OBJS) $(IO_OPTIMIZER_OBJS)

# 目标可执行文件
TARGET = kvserver
SHARED_LIB = libkvstore.so
TEST_TARGET = kv_engine_test
BTREE_TEST_TARGET = btree_test
DEBUG_BTREE_TARGET = debug_btree
SIMPLE_DEBUG_TARGET = simple_debug
PRODUCTION_TEST_TARGET = btree_production_test
LSM_MANAGER_TEST_TARGET = lsm_memtable_manager_test
LSM_MEMTABLE_TEST_TARGET = lsm_memtable_test
CACHE_TEST_TARGET = kv_cache_test
CACHE_COMPREHENSIVE_TEST_TARGET = kv_cache_comprehensive_test
CACHE_EXAMPLE_TARGET = cache_example
IO_OPTIMIZER_TEST_TARGET = kv_io_optimizer_test

# 默认目标
all: $(TARGET) $(SHARED_LIB)

# 静态编译可执行文件
$(TARGET): $(ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译共享库
$(SHARED_LIB): $(ALL_OBJS)
	$(CC) -shared $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译测试程序
$(TEST_TARGET): kv_engine_test.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译B+Tree测试程序
$(BTREE_TEST_TARGET): ../tests/kvserver_tests/btree_tests/btree_test.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译B+Tree调试程序
$(DEBUG_BTREE_TARGET): ../tests/kvserver_tests/btree_tests/debug_btree.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译简单调试程序
$(SIMPLE_DEBUG_TARGET): ../tests/kvserver_tests/btree_tests/simple_test.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译生产环境测试程序
$(PRODUCTION_TEST_TARGET): ../tests/kvserver_tests/btree_tests/btree_production_test.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译LSM-Tree管理器测试程序
$(LSM_MANAGER_TEST_TARGET): ../tests/kvserver_tests/lsm_tests/lsm_memtable_manager_test.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译LSM-Tree内存表测试程序
$(LSM_MEMTABLE_TEST_TARGET): ../tests/kvserver_tests/lsm_tests/lsm_memtable_test.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译缓存层测试程序
$(CACHE_TEST_TARGET): ../tests/kvserver_tests/cache_tests/kv_cache_test.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译缓存层综合测试程序
$(CACHE_COMPREHENSIVE_TEST_TARGET): ../tests/kvserver_tests/cache_tests/kv_cache_comprehensive_test.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译缓存层示例程序
$(CACHE_EXAMPLE_TARGET): ../examples/cache_example.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译I/O优化器测试程序 🚀
$(IO_OPTIMIZER_TEST_TARGET): ../tests/kvserver_tests/io_optimizer_tests/kv_io_optimizer_test.o $(ALL_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 规则：从.c文件生成.o文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 编译tests目录下的.c文件
../tests/%.o: ../tests/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 编译tests/kvserver_tests目录下的.c文件
../tests/kvserver_tests/%.o: ../tests/kvserver_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译tests/kvserver_tests/btree_tests目录下的.c文件
../tests/kvserver_tests/btree_tests/%.o: ../tests/kvserver_tests/btree_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译tests/kvserver_tests/hash_tests目录下的.c文件
../tests/kvserver_tests/hash_tests/%.o: ../tests/kvserver_tests/hash_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译tests/kvserver_tests/rbtree_tests目录下的.c文件
../tests/kvserver_tests/rbtree_tests/%.o: ../tests/kvserver_tests/rbtree_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译tests/kvserver_tests/array_tests目录下的.c文件
../tests/kvserver_tests/array_tests/%.o: ../tests/kvserver_tests/array_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译tests/kvserver_tests/system_tests目录下的.c文件
../tests/kvserver_tests/system_tests/%.o: ../tests/kvserver_tests/system_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译tests/kvserver_tests/engine_tests目录下的.c文件
../tests/kvserver_tests/engine_tests/%.o: ../tests/kvserver_tests/engine_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译tests/kvserver_tests/lsm_tests目录下的.c文件 🔥
../tests/kvserver_tests/lsm_tests/%.o: ../tests/kvserver_tests/lsm_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译tests/kvserver_tests/cache_tests目录下的.c文件 ⚡
../tests/kvserver_tests/cache_tests/%.o: ../tests/kvserver_tests/cache_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译tests/kvserver_tests/io_optimizer_tests目录下的.c文件 🚀
../tests/kvserver_tests/io_optimizer_tests/%.o: ../tests/kvserver_tests/io_optimizer_tests/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 编译examples目录下的.c文件
../examples/%.o: ../examples/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# 运行测试
test: $(TEST_TARGET)
	@echo "🚀 运行存储引擎接口测试..."
	./$(TEST_TARGET)

# 运行B+Tree测试
run_btree_test: $(BTREE_TEST_TARGET)
	@echo "🌳 运行B+Tree存储引擎测试..."
	./$(BTREE_TEST_TARGET)

# 运行B+Tree调试程序
run_debug_btree: $(DEBUG_BTREE_TARGET)
	@echo "🐛 运行B+Tree调试程序..."
	./$(DEBUG_BTREE_TARGET)

# 运行简单调试程序
run_simple_debug: $(SIMPLE_DEBUG_TARGET)
	@echo "🔍 运行简单调试程序..."
	./$(SIMPLE_DEBUG_TARGET)

# 运行生产环境测试
run_production_test: $(PRODUCTION_TEST_TARGET)
	@echo "🏭 运行B+Tree生产环境级别检查..."
	./$(PRODUCTION_TEST_TARGET)

# 运行LSM-Tree管理器测试
run_lsm_manager_test: $(LSM_MANAGER_TEST_TARGET)
	@echo "🎯 运行LSM-Tree管理器测试..."
	./$(LSM_MANAGER_TEST_TARGET)

# 运行LSM-Tree内存表测试
run_lsm_memtable_test: $(LSM_MEMTABLE_TEST_TARGET)
	@echo "🎯 运行LSM-Tree内存表测试..."
	./$(LSM_MEMTABLE_TEST_TARGET)

# 运行缓存层测试
run_cache_test: $(CACHE_TEST_TARGET)
	@echo "⚡ 运行缓存层基础测试..."
	./$(CACHE_TEST_TARGET)

# 运行缓存层综合测试
run_cache_comprehensive_test: $(CACHE_COMPREHENSIVE_TEST_TARGET)
	@echo "⚡ 运行缓存层综合测试..."
	./$(CACHE_COMPREHENSIVE_TEST_TARGET)

# 运行I/O优化器测试 🚀
run_io_optimizer_test: $(IO_OPTIMIZER_TEST_TARGET)
	@echo "🚀 运行I/O优化器测试..."
	./$(IO_OPTIMIZER_TEST_TARGET)

# 运行LSM-Tree测试
test_lsm: run_lsm_manager_test run_lsm_memtable_test
	@echo "🔥 LSM-Tree测试完成"

# 运行缓存层测试
test_cache: run_cache_test run_cache_comprehensive_test
	@echo "⚡ 缓存层测试完成"

# 运行I/O优化器测试
test_io_optimizer: run_io_optimizer_test
	@echo "🚀 I/O优化器测试完成"

# 运行缓存层示例
run_cache_example: $(CACHE_EXAMPLE_TARGET)
	@echo "🎯 运行缓存层功能演示..."
	./$(CACHE_EXAMPLE_TARGET)

# 运行所有测试
test_all: test run_btree_test run_lsm_manager_test run_lsm_memtable_test test_cache test_io_optimizer
	@echo "✅ 所有测试完成"

# 运行Hash存储引擎测试
test_hash:
	@echo "🗃️  运行Hash存储引擎测试..."
	@cd ../tests/kvserver_tests/hash_tests && ./run_all_hash_tests.sh

# 运行RBTree存储引擎测试
test_rbtree:
	@echo "🌲 运行RBTree存储引擎测试..."
	@echo "RBTree测试需要手动编译和运行"

# 运行Array存储引擎测试
test_array:
	@echo "📊 运行Array存储引擎测试..."
	@echo "Array测试需要手动编译和运行"

# 运行系统功能测试
test_system:
	@echo "⚙️  运行系统功能测试..."
	@echo "系统测试需要手动编译和运行"

# 运行引擎接口测试
test_engine_interface:
	@echo "🔧 运行引擎接口测试..."
	@echo "引擎接口测试需要手动编译和运行"

# 运行分类测试汇总
test_by_category: test_hash test_rbtree test_array test_system test_engine_interface run_btree_test test_lsm test_cache test_io_optimizer
	@echo "🎯 所有分类测试完成"

# 性能测试（更大数据量）
perf_test: $(TEST_TARGET)
	@echo "⚡ 运行性能测试..."
	./$(TEST_TARGET) --performance

# 验证编译（检查所有头文件和接口）
verify: $(ALL_OBJS)
	@echo "✅ 所有源文件编译成功"
	@echo "📋 编译的对象文件:"
	@ls -la *.o
	@echo "🔗 检查符号表..."
	@nm -g $(SHARED_LIB) | grep -E "kv_engine|kvs_|btree_" | head -15

# 清理
clean:
	rm -f $(TARGET) $(SHARED_LIB) $(TEST_TARGET) $(BTREE_TEST_TARGET) $(DEBUG_BTREE_TARGET) $(SIMPLE_DEBUG_TARGET) $(PRODUCTION_TEST_TARGET) $(LSM_MANAGER_TEST_TARGET) $(LSM_MEMTABLE_TEST_TARGET) *.o ../tests/*.o ../tests/kvserver_tests/*.o

# 深度清理（包括备份文件）
clean_all: clean
	rm -f *~ *.bak *.swp core.*

# 安装
install: $(TARGET) $(SHARED_LIB)
	mkdir -p $(DESTDIR)/usr/local/bin
	mkdir -p $(DESTDIR)/usr/local/lib
	mkdir -p $(DESTDIR)/usr/local/include/concordkv
	cp $(TARGET) $(DESTDIR)/usr/local/bin/
	cp $(SHARED_LIB) $(DESTDIR)/usr/local/lib/
	cp kv_engine_interface.h $(DESTDIR)/usr/local/include/concordkv/
	cp kv_store.h $(DESTDIR)/usr/local/include/concordkv/
	cp kv_error.h $(DESTDIR)/usr/local/include/concordkv/
	ldconfig

# 卸载
uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
	rm -f $(DESTDIR)/usr/local/lib/$(SHARED_LIB)
	rm -rf $(DESTDIR)/usr/local/include/concordkv
	ldconfig

# 代码检查（使用静态分析工具）
check:
	@echo "🔍 运行代码检查..."
	@which cppcheck > /dev/null && cppcheck --enable=all --std=c99 *.c || echo "安装cppcheck以启用代码检查"
	@which valgrind > /dev/null && echo "✅ Valgrind可用于内存检查" || echo "安装valgrind以启用内存检查"

# 内存检查
memcheck: $(TEST_TARGET)
	@echo "🔍 运行内存泄漏检查..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TEST_TARGET)

# 调试版本
debug: CFLAGS += -DDEBUG -O0
debug: clean $(TARGET) $(TEST_TARGET)
	@echo "🐛 调试版本编译完成"

# 发布版本
release: CFLAGS += -DNDEBUG -O3
release: clean $(TARGET) $(SHARED_LIB)
	@echo "🚀 发布版本编译完成"
	strip $(TARGET) $(SHARED_LIB)

# 生成文档
doc:
	@echo "📚 生成API文档..."
	@which doxygen > /dev/null && doxygen Doxyfile || echo "安装doxygen以生成文档"

# 代码格式化
format:
	@echo "🎨 格式化代码..."
	@which clang-format > /dev/null && find . -name "*.c" -o -name "*.h" | xargs clang-format -i || echo "安装clang-format以格式化代码"

# 显示编译信息
info:
	@echo "📋 编译信息:"
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "目标文件: $(ALL_OBJS)"
	@echo "可执行文件: $(TARGET)"
	@echo "共享库: $(SHARED_LIB)"
	@echo "测试程序: $(TEST_TARGET)"
	@echo "B+Tree测试程序: $(BTREE_TEST_TARGET)"

# 帮助信息
help:
	@echo "🆘 可用的make目标:"
	@echo "  all       - 编译所有目标（默认）"
	@echo "  test      - 运行基础测试"
	@echo "  test_all  - 运行所有核心测试"
	@echo "  test_by_category - 运行所有分类测试"
	@echo ""
	@echo "🎯 分类测试:"
	@echo "  run_btree_test     - B+Tree存储引擎测试"
	@echo "  run_production_test - B+Tree生产环境检查"
	@echo "  test_lsm           - LSM-Tree存储引擎测试"
	@echo "  test_cache         - 缓存层功能测试"
	@echo "  test_io_optimizer  - I/O优化器功能测试"
	@echo "  test_hash          - Hash存储引擎测试"
	@echo "  test_rbtree        - RBTree存储引擎测试"
	@echo "  test_array         - Array存储引擎测试"
	@echo "  test_system        - 系统功能测试"
	@echo "  test_engine_interface - 引擎接口测试"
	@echo ""
	@echo "🔧 其他目标:"
	@echo "  perf_test - 运行性能测试"
	@echo "  verify    - 验证编译结果"
	@echo "  debug     - 编译调试版本"
	@echo "  release   - 编译发布版本"
	@echo "  check     - 代码静态检查"
	@echo "  memcheck  - 内存泄漏检查"
	@echo "  format    - 代码格式化"
	@echo "  doc       - 生成文档"
	@echo "  clean     - 清理编译产物"
	@echo "  clean_all - 深度清理"
	@echo "  install   - 安装到系统"
	@echo "  uninstall - 从系统卸载"
	@echo "  info      - 显示编译信息"
	@echo "  help      - 显示此帮助"

# 声明伪目标
.PHONY: all clean clean_all install uninstall test perf_test verify check memcheck debug release doc format info help
