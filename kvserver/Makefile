# 编译器和选项
CC = gcc
CFLAGS = -Wall -g -O2 -fPIC -std=c99
LDFLAGS = -lpthread

# 源文件和目标文件
CORE_OBJS = kvstore.o kvstore_array.o kvstore_hash.o kvstore_rbtree.o rbtree_adapter.o \
            kv_persist.o kv_concurrency.o kv_transaction.o kv_error.o kv_cluster.o \
            ntyco_entry.o epoll_entry.o

# 新增的统一引擎接口相关文件
ENGINE_OBJS = kv_engine_factory.o

# 所有目标文件
ALL_OBJS = $(CORE_OBJS) $(ENGINE_OBJS)

# 目标可执行文件
TARGET = kvserver
SHARED_LIB = libkvstore.so
TEST_TARGET = kv_engine_test

# 默认目标
all: $(TARGET) $(SHARED_LIB)

# 静态编译可执行文件
$(TARGET): $(ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译共享库
$(SHARED_LIB): $(ALL_OBJS)
	$(CC) -shared $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译测试程序
$(TEST_TARGET): kv_engine_test.o $(ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 规则：从.c文件生成.o文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 运行测试
test: $(TEST_TARGET)
	@echo "🚀 运行存储引擎接口测试..."
	./$(TEST_TARGET)

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
	@nm -g $(SHARED_LIB) | grep -E "kv_engine|kvs_" | head -10

# 清理
clean:
	rm -f $(TARGET) $(SHARED_LIB) $(TEST_TARGET) *.o

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

# 帮助信息
help:
	@echo "🆘 可用的make目标:"
	@echo "  all       - 编译所有目标（默认）"
	@echo "  test      - 运行基础测试"
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
