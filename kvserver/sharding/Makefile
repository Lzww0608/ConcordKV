# ConcordKV 分片管理集成 Makefile
# @Author: Lzww0608
# @Date: 2025-01-08 20:50:00
# 复用现有存储引擎实现

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -fPIC -O2 -D_GNU_SOURCE
INCLUDES = -I.. -I../../common/util -I../../common/config
LIBS = -lm -lpthread

# 调试模式配置
DEBUG_CFLAGS = -g -DDEBUG -O0
RELEASE_CFLAGS = -O3 -DNDEBUG

# 源文件 (移除mock文件)
SHARD_SOURCES = shard_hash.c shard_config.c shard_aware_engine.c
SHARD_OBJECTS = $(SHARD_SOURCES:.c=.o)

# 现有kvserver核心源文件 (包含metrics mock避免链接问题)
KVSERVER_DIR = ..
KVSERVER_SOURCES = \
	$(KVSERVER_DIR)/kv_engine_factory.c \
	$(KVSERVER_DIR)/kv_engine_config.c \
	$(KVSERVER_DIR)/kv_memory.c \
	$(KVSERVER_DIR)/kvstore.c \
	$(KVSERVER_DIR)/kvstore_array.c \
	$(KVSERVER_DIR)/kvstore_hash.c \
	$(KVSERVER_DIR)/kvstore_btree.c \
	$(KVSERVER_DIR)/kvstore_rbtree.c \
	$(KVSERVER_DIR)/rbtree_adapter.c \
	$(KVSERVER_DIR)/btree_adapter.c \
	$(KVSERVER_DIR)/kv_error.c \
	kv_engine_metrics_mock.c

KVSERVER_OBJECTS = $(KVSERVER_SOURCES:.c=.o)

# 测试文件
TEST_SOURCES = tests/test_shard_integration.c
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)

# 目标库和可执行文件
SHARD_LIB = libshard.a
KVSERVER_LIB = libkvserver.a
TEST_EXEC = test_shard_integration

# 依赖模块
COMMON_CONFIG_DIR = ../../common/config
CONFIG_SOURCES = $(COMMON_CONFIG_DIR)/config.c $(COMMON_CONFIG_DIR)/parser.c
CONFIG_OBJECTS = $(CONFIG_SOURCES:.c=.o)

# 扩展kvserver依赖源文件
EXTENDED_KVSERVER_SOURCES = \
	../kv_engine_factory.c \
	../kv_engine_config.c \
	../kv_memory.c \
	../kv_error.c \
	../kvstore_array.c \
	../kvstore_hash.c \
	../kvstore_btree.c \
	../kvstore_rbtree.c \
	../rbtree_adapter.c \
	../btree_adapter.c

EXTENDED_KVSERVER_OBJECTS = $(EXTENDED_KVSERVER_SOURCES:.c=.o)

# 默认目标
all: $(SHARD_LIB) $(KVSERVER_LIB)

# 分片管理库
$(SHARD_LIB): $(SHARD_OBJECTS)
	@echo "📦 Creating shard management library..."
	ar rcs $@ $^
	@echo "✅ Library created: $@"

# kvserver库
$(KVSERVER_LIB): $(KVSERVER_OBJECTS)
	@echo "📦 Creating kvserver library..."
	ar rcs $@ $^
	@echo "✅ Library created: $@"

# 编译分片管理源文件
%.o: %.c
	@echo "🔨 Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 编译kvserver源文件
$(KVSERVER_DIR)/%.o: $(KVSERVER_DIR)/%.c
	@echo "🔨 Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 简化测试
simple_test: simple_test_exec
	@echo "🧪 Running simple tests..."
	./simple_test

simple_test_exec: tests/simple_test.o $(SHARD_LIB) $(EXTENDED_KVSERVER_OBJECTS) $(CONFIG_OBJECTS) kv_engine_metrics_mock.o
	@echo "🧪 Building simple test executable..."
	$(CC) $(CFLAGS) $(DEBUG_CFLAGS) $(INCLUDES) -o simple_test $^ $(LIBS)
	@echo "✅ Simple test executable created: simple_test"

tests/simple_test.o: tests/simple_test.c
	@echo "🔨 Compiling tests/simple_test.c..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 测试目标
test: $(TEST_EXEC)
	@echo "🧪 Running integration tests..."
	./$(TEST_EXEC)

# 测试可执行文件
$(TEST_EXEC): $(TEST_OBJECTS) $(SHARD_LIB) $(KVSERVER_LIB)
	@echo "🧪 Building test executable..."
	$(CC) $(CFLAGS) $(DEBUG_CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)
	@echo "✅ Test executable created: $@"

# 调试版本
debug: CFLAGS += $(DEBUG_CFLAGS)
debug: all

# 发布版本
release: CFLAGS += $(RELEASE_CFLAGS)
release: all

# 语法检查
lint:
	@echo "📝 Running syntax check..."
	$(CC) $(CFLAGS) $(INCLUDES) -fsyntax-only $(SHARD_SOURCES)
	@echo "✅ Syntax check passed"

# 清理
clean:
	@echo "🧹 Cleaning up..."
	rm -f $(SHARD_OBJECTS) $(KVSERVER_OBJECTS) $(TEST_OBJECTS) $(SHARD_LIB) $(KVSERVER_LIB) $(TEST_EXEC)
	@echo "✅ Cleanup completed"

# 代码统计
stats:
	@echo "📊 Code statistics:"
	@echo "   📄 Source files: $(words $(SHARD_SOURCES))"
	@echo "   📏 Lines of code:"
	@wc -l $(SHARD_SOURCES) $(wildcard *.h) | tail -1
	@echo "   📦 Object files: $(words $(SHARD_OBJECTS))"

# 帮助信息
help:
	@echo "🚀 ConcordKV Sharding Management Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all       - Build all libraries (default)"
	@echo "  test      - Build and run tests"
	@echo "  debug     - Build with debug symbols"
	@echo "  release   - Build optimized release version"
	@echo "  lint      - Run syntax check"
	@echo "  clean     - Clean build artifacts"
	@echo "  stats     - Show code statistics"
	@echo "  help      - Show this help message"

# 伪目标
.PHONY: all test debug release lint clean stats help

# 依赖关系
shard_hash.o: shard_hash.h
shard_config.o: shard_config.h shard_hash.h
shard_aware_engine.o: shard_aware_engine.h shard_config.h shard_hash.h
tests/test_shard_integration.o: shard_aware_engine.h shard_config.h shard_hash.h

# 编译config模块
$(COMMON_CONFIG_DIR)/%.o: $(COMMON_CONFIG_DIR)/%.c
	@echo "🔨 Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
