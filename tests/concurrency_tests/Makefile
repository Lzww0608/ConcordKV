CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -O2 -g
LDFLAGS = -pthread -lrt

# 源文件目录
KVSERVER_DIR = ../../kvserver
TEST_DIR = .

# 源文件
CONCURRENCY_SOURCES = $(KVSERVER_DIR)/kv_concurrency.c
TEST_SOURCES = test_deadlock_detection.c test_lock_stress.c

# 目标文件
CONCURRENCY_OBJECTS = $(CONCURRENCY_SOURCES:.c=.o)
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)

# 可执行文件
EXECUTABLES = test_deadlock_detection test_lock_stress

# 默认目标
all: $(EXECUTABLES)

# 编译并发控制模块
$(KVSERVER_DIR)/kv_concurrency.o: $(KVSERVER_DIR)/kv_concurrency.c $(KVSERVER_DIR)/kv_concurrency.h
	$(CC) $(CFLAGS) -c $< -o $@

# 编译死锁检测测试
test_deadlock_detection: test_deadlock_detection.o $(KVSERVER_DIR)/kv_concurrency.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_deadlock_detection.o: test_deadlock_detection.c $(KVSERVER_DIR)/kv_concurrency.h
	$(CC) $(CFLAGS) -c $< -o $@

# 编译压力测试
test_lock_stress: test_lock_stress.o $(KVSERVER_DIR)/kv_concurrency.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_lock_stress.o: test_lock_stress.c $(KVSERVER_DIR)/kv_concurrency.h
	$(CC) $(CFLAGS) -c $< -o $@

# 运行测试
test: all
	@echo "运行死锁检测测试..."
	./test_deadlock_detection
	@echo ""
	@echo "运行压力测试（8线程，100键，70%读操作）..."
	./test_lock_stress 8 100 70

# 运行快速测试
quick-test: all
	@echo "运行快速死锁检测测试..."
	timeout 30s ./test_deadlock_detection || true
	@echo ""
	@echo "运行快速压力测试（4线程，50键，80%读操作）..."
	timeout 15s ./test_lock_stress 4 50 80 || true

# 性能基准测试
benchmark: all
	@echo "运行性能基准测试..."
	@echo "测试配置: 不同线程数量的性能对比"
	@for threads in 1 2 4 8 16; do \
		echo "测试 $$threads 线程..."; \
		timeout 10s ./test_lock_stress $$threads 100 70 | grep "吞吐量" || true; \
	done

# 清理
clean:
	rm -f $(TEST_OBJECTS) $(CONCURRENCY_OBJECTS) $(EXECUTABLES)
	rm -f *.o

# 深度清理
distclean: clean
	rm -f core core.*

# 安装依赖检查
check-deps:
	@echo "检查编译依赖..."
	@which gcc > /dev/null || (echo "错误: 需要安装 gcc" && exit 1)
	@echo "依赖检查通过"

# 调试版本
debug: CFLAGS += -DDEBUG -g3 -O0
debug: all

# 发布版本
release: CFLAGS += -DNDEBUG -O3
release: all

# 内存检查（需要valgrind）
memcheck: all
	@echo "运行内存检查..."
	@which valgrind > /dev/null || (echo "警告: 未安装 valgrind，跳过内存检查" && exit 0)
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
		--track-origins=yes --verbose ./test_deadlock_detection
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
		--track-origins=yes --verbose ./test_lock_stress 2 10 80

# 线程检查（需要valgrind）
threadcheck: all
	@echo "运行线程检查..."
	@which valgrind > /dev/null || (echo "警告: 未安装 valgrind，跳过线程检查" && exit 0)
	valgrind --tool=helgrind ./test_deadlock_detection
	valgrind --tool=helgrind ./test_lock_stress 2 10 80

# 帮助信息
help:
	@echo "可用的make目标:"
	@echo "  all          - 编译所有测试程序"
	@echo "  test         - 运行完整测试套件"
	@echo "  quick-test   - 运行快速测试"
	@echo "  benchmark    - 运行性能基准测试"
	@echo "  debug        - 编译调试版本"
	@echo "  release      - 编译发布版本"
	@echo "  memcheck     - 运行内存检查（需要valgrind）"
	@echo "  threadcheck  - 运行线程检查（需要valgrind）"
	@echo "  clean        - 清理编译文件"
	@echo "  distclean    - 深度清理"
	@echo "  check-deps   - 检查编译依赖"
	@echo "  help         - 显示此帮助信息"
	@echo ""
	@echo "测试程序使用方法:"
	@echo "  ./test_deadlock_detection"
	@echo "  ./test_lock_stress [线程数] [键数] [读操作百分比]"
	@echo ""
	@echo "示例:"
	@echo "  ./test_lock_stress 8 100 70  # 8线程，100键，70%读操作"

.PHONY: all test quick-test benchmark clean distclean check-deps debug release memcheck threadcheck help 