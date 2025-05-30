# 测试项目的 Makefile
CC = gcc
CFLAGS = -Wall -g -O2 -fPIC -std=c99 -I../kvserver
LDFLAGS = -lpthread

# 源文件路径
KVSERVER_DIR = ../kvserver
ENGINE_TEST_DIR = engine_tests

# 需要链接的kvserver对象文件
KVSERVER_OBJS = $(KVSERVER_DIR)/kv_memory.o $(KVSERVER_DIR)/kvstore_array.o $(KVSERVER_DIR)/kvstore_hash.o \
                $(KVSERVER_DIR)/kvstore_rbtree.o $(KVSERVER_DIR)/rbtree_adapter.o \
                $(KVSERVER_DIR)/kv_persist.o $(KVSERVER_DIR)/kv_concurrency.o \
                $(KVSERVER_DIR)/kv_transaction.o $(KVSERVER_DIR)/kv_error.o \
                $(KVSERVER_DIR)/kv_cluster.o $(KVSERVER_DIR)/kv_engine_factory.o

# 测试目标
ENGINE_TEST = $(ENGINE_TEST_DIR)/kv_engine_test_enhanced
ORIGINAL_TEST = $(ENGINE_TEST_DIR)/kv_engine_test_original

# 默认目标
all: $(ENGINE_TEST) $(ORIGINAL_TEST)

# 增强版测试程序（包含超时机制）
$(ENGINE_TEST): $(ENGINE_TEST_DIR)/kv_engine_test_enhanced.o $(KVSERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 原版测试程序（从kvserver复制）
$(ENGINE_TEST_DIR)/kv_engine_test_original.c: $(KVSERVER_DIR)/kv_engine_test.c
	cp $< $@

$(ORIGINAL_TEST): $(ENGINE_TEST_DIR)/kv_engine_test_original.o $(KVSERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译规则
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 确保kvserver对象文件存在
$(KVSERVER_OBJS):
	$(MAKE) -C $(KVSERVER_DIR) $(notdir $@)

# 运行测试
test: $(ENGINE_TEST)
	@echo "🚀 运行增强版存储引擎测试（包含超时机制）..."
	./$(ENGINE_TEST)

test_original: $(ORIGINAL_TEST)
	@echo "🚀 运行原版存储引擎测试..."
	./$(ORIGINAL_TEST)

# 运行所有测试
test_all: test test_original
	@echo "🎉 所有测试完成"

# 内存检查
memcheck: $(ENGINE_TEST)
	@echo "🔍 运行内存泄漏检查..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
	         --timeout=120 ./$(ENGINE_TEST)

# 清理
clean:
	rm -f $(ENGINE_TEST) $(ORIGINAL_TEST)
	rm -f $(ENGINE_TEST_DIR)/*.o
	rm -f $(ENGINE_TEST_DIR)/kv_engine_test_original.c

# 深度清理
clean_all: clean
	rm -f $(ENGINE_TEST_DIR)/*~ $(ENGINE_TEST_DIR)/*.bak $(ENGINE_TEST_DIR)/*.swp

# 安装（创建符号链接到系统测试目录）
install:
	@echo "📦 安装测试程序到 /usr/local/bin/concordkv-tests/"
	sudo mkdir -p /usr/local/bin/concordkv-tests/
	sudo cp $(ENGINE_TEST) /usr/local/bin/concordkv-tests/
	sudo chmod +x /usr/local/bin/concordkv-tests/$(notdir $(ENGINE_TEST))

# 卸载
uninstall:
	sudo rm -rf /usr/local/bin/concordkv-tests/

# 性能基准测试
benchmark: $(ENGINE_TEST)
	@echo "⚡ 运行性能基准测试..."
	./$(ENGINE_TEST) --benchmark

# 并发压力测试
stress: $(ENGINE_TEST)
	@echo "💪 运行并发压力测试..."
	./$(ENGINE_TEST) --stress

# 帮助信息
help:
	@echo "🆘 ConcordKV 测试系统可用目标:"
	@echo "  all           - 编译所有测试程序"
	@echo "  test          - 运行增强版测试（带超时）"
	@echo "  test_original - 运行原版测试"
	@echo "  test_all      - 运行所有测试"
	@echo "  memcheck      - 内存泄漏检查"
	@echo "  benchmark     - 性能基准测试"
	@echo "  stress        - 并发压力测试"
	@echo "  clean         - 清理编译产物"
	@echo "  clean_all     - 深度清理"
	@echo "  install       - 安装测试程序"
	@echo "  uninstall     - 卸载测试程序"
	@echo "  help          - 显示此帮助"

# 显示测试信息
info:
	@echo "📋 测试系统信息:"
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "测试程序: $(ENGINE_TEST) $(ORIGINAL_TEST)"
	@echo "依赖对象: $(KVSERVER_OBJS)"

.PHONY: all test test_original test_all memcheck clean clean_all install uninstall benchmark stress help info 