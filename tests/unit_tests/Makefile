CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2 -pthread
INCLUDES = -I../../kvserver
LIBS = -lpthread

# 源文件
KVSERVER_DIR = ../../kvserver
ENHANCED_SOURCES = $(KVSERVER_DIR)/kv_persist_enhanced.c $(KVSERVER_DIR)/kv_snapshot_enhanced.c

# 目标文件
ENHANCED_OBJECTS = $(ENHANCED_SOURCES:.c=.o)

# 可执行文件
TESTS = simple_test

.PHONY: all clean test

all: $(TESTS)

simple_test: simple_test.c $(ENHANCED_OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test: $(TESTS)
	@echo "运行单元测试..."
	@./simple_test

clean:
	rm -f $(TESTS) $(ENHANCED_OBJECTS)
	rm -rf /tmp/simple_test

help:
	@echo "可用的目标:"
	@echo "  all     - 编译所有单元测试"
	@echo "  test    - 运行单元测试"
	@echo "  clean   - 清理编译文件"
	@echo "  help    - 显示此帮助信息" 