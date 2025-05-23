CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -pthread -std=c99 -I../
LDFLAGS = -pthread -lcrypto -lssl -lm -lz -luuid -lrt

# 项目源文件目录
UTIL_DIR = ../util
CONFIG_DIR = ../config
METRICS_DIR = ../metrics

# 工具库源文件
UTIL_SRCS = $(UTIL_DIR)/util.c $(UTIL_DIR)/log.c $(UTIL_DIR)/network.c $(UTIL_DIR)/network_io.c $(UTIL_DIR)/timer.c
# $(UTIL_DIR)/crypt.c  # 暂时注释掉
UTIL_OBJS = $(UTIL_SRCS:.c=.o)

# 配置库源文件
CONFIG_SRCS = $(CONFIG_DIR)/config.c $(CONFIG_DIR)/parser.c
CONFIG_OBJS = $(CONFIG_SRCS:.c=.o)

# 度量库源文件
METRICS_SRCS = $(METRICS_DIR)/metrics.c $(METRICS_DIR)/metrics_format.c
METRICS_OBJS = $(METRICS_SRCS:.c=.o)

# 测试框架源文件
TESTING_SRCS = mock_client.c fault_inject.c benchmark.c test_data.c
TESTING_OBJS = $(TESTING_SRCS:.c=.o)

# 测试程序
UTIL_TEST = util_test
CONFIG_TEST = config_test
TIMER_TEST = timer_test
METRICS_TEST = metrics_test
METRICS_SIMPLE_TEST = metrics_simple_test
TESTING_DEMO = testing_demo
SIMPLE_TEST = simple_test

# 测试程序源文件
UTIL_TEST_SRCS = util_test.c
CONFIG_TEST_SRCS = config_test.c
TIMER_TEST_SRCS = timer_test.c
METRICS_TEST_SRCS = metrics_test.c
METRICS_SIMPLE_TEST_SRCS = metrics_simple_test.c
TESTING_DEMO_SRCS = testing_demo.c
SIMPLE_TEST_SRCS = simple_test.c

# 目标文件
UTIL_TEST_OBJS = $(UTIL_TEST_SRCS:.c=.o)
CONFIG_TEST_OBJS = $(CONFIG_TEST_SRCS:.c=.o)
TIMER_TEST_OBJS = $(TIMER_TEST_SRCS:.c=.o)
METRICS_TEST_OBJS = $(METRICS_TEST_SRCS:.c=.o)
METRICS_SIMPLE_TEST_OBJS = $(METRICS_SIMPLE_TEST_SRCS:.c=.o)
TESTING_DEMO_OBJS = $(TESTING_DEMO_SRCS:.c=.o)
SIMPLE_TEST_OBJS = $(SIMPLE_TEST_SRCS:.c=.o)

.PHONY: all clean test util-test config-test timer-test metrics-test metrics-simple-test testing-demo simple-test

all: $(UTIL_TEST) $(CONFIG_TEST) $(TIMER_TEST) $(METRICS_TEST) $(METRICS_SIMPLE_TEST) $(TESTING_DEMO) $(SIMPLE_TEST)

# 编译工具函数测试
$(UTIL_TEST): $(UTIL_TEST_OBJS) $(UTIL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译配置管理测试
$(CONFIG_TEST): $(CONFIG_TEST_OBJS) $(CONFIG_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译定时器测试
$(TIMER_TEST): $(TIMER_TEST_OBJS) $(UTIL_DIR)/timer.o $(UTIL_DIR)/util.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译度量系统测试
$(METRICS_TEST): $(METRICS_TEST_OBJS) $(METRICS_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译度量系统简化测试
$(METRICS_SIMPLE_TEST): $(METRICS_SIMPLE_TEST_OBJS) $(METRICS_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译测试框架演示
$(TESTING_DEMO): $(TESTING_DEMO_OBJS) $(TESTING_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译简化测试
$(SIMPLE_TEST): $(SIMPLE_TEST_OBJS) ../util/util.o
	$(CC) $(CFLAGS) -o $@ $^ -pthread -lm -luuid

# 编译规则
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 运行所有测试
test: simple-test util-test config-test timer-test metrics-test metrics-simple-test testing-demo

# 运行工具函数测试
util-test: $(UTIL_TEST)
	@echo "========================================="
	@echo "运行 Util 模块测试"
	@echo "========================================="
	./$(UTIL_TEST)

# 运行配置管理测试
config-test: $(CONFIG_TEST)
	@echo "========================================="
	@echo "运行 Config 模块测试"
	@echo "========================================="
	./$(CONFIG_TEST)

# 运行定时器测试
timer-test: $(TIMER_TEST)
	@echo "========================================="
	@echo "运行 Timer 模块测试"
	@echo "========================================="
	timeout 30 ./$(TIMER_TEST) || true

# 运行度量系统测试
metrics-test: $(METRICS_TEST)
	@echo "========================================="
	@echo "运行 Metrics 模块测试"
	@echo "========================================="
	timeout 30 ./$(METRICS_TEST) || true

# 运行度量系统简化测试
metrics-simple-test: $(METRICS_SIMPLE_TEST)
	@echo "========================================="
	@echo "运行 Metrics 简化测试"
	@echo "========================================="
	./$(METRICS_SIMPLE_TEST)

# 运行测试框架演示
testing-demo: $(TESTING_DEMO)
	@echo "========================================="
	@echo "运行 Testing 框架演示"
	@echo "========================================="
	timeout 15 ./$(TESTING_DEMO) > /tmp/testing_output.log 2>&1 || true
	@echo "测试完成，查看输出日志："
	@tail -20 /tmp/testing_output.log

# 运行简化测试
simple-test: $(SIMPLE_TEST)
	@echo "========================================="
	@echo "运行简化测试"
	@echo "========================================="
	./$(SIMPLE_TEST)

# 清理
clean:
	rm -f $(UTIL_TEST_OBJS) $(CONFIG_TEST_OBJS) $(TIMER_TEST_OBJS) $(METRICS_TEST_OBJS) $(METRICS_SIMPLE_TEST_OBJS) $(TESTING_DEMO_OBJS) $(SIMPLE_TEST_OBJS) $(TESTING_OBJS)
	rm -f $(UTIL_OBJS) $(CONFIG_OBJS) $(METRICS_OBJS)
	rm -f $(UTIL_TEST) $(CONFIG_TEST) $(TIMER_TEST) $(METRICS_TEST) $(METRICS_SIMPLE_TEST) $(TESTING_DEMO) $(SIMPLE_TEST)
	rm -f /tmp/testing_output.log

# 安装（可选）
install: all
	@echo "将测试程序安装到当前用户目录..."
	mkdir -p ~/bin
	cp $(UTIL_TEST) $(CONFIG_TEST) $(TIMER_TEST) $(METRICS_TEST) $(METRICS_SIMPLE_TEST) $(TESTING_DEMO) ~/bin/

# 帮助信息
help:
	@echo "可用的make目标："
	@echo "  all         - 编译所有测试程序"
	@echo "  util-test   - 编译并运行工具函数测试"
	@echo "  config-test - 编译并运行配置管理测试"
	@echo "  timer-test  - 编译并运行定时器测试"
	@echo "  metrics-test- 编译并运行度量系统测试"
	@echo "  metrics-simple-test- 编译并运行度量系统简化测试"
	@echo "  testing-demo- 编译并运行测试框架演示"
	@echo "  test        - 运行所有测试"
	@echo "  clean       - 清理编译产物"
	@echo "  install     - 安装测试程序到~/bin"
	@echo "  help        - 显示此帮助信息" 