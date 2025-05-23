CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -pthread -fPIC -std=c99
LDFLAGS = -pthread -lrt -lm -lcrypto -lssl -lz -luuid

# 主要源文件
UTIL_SRCS = util.c log.c network.c network_io.c timer.c crypt.c
UTIL_OBJS = $(UTIL_SRCS:.c=.o)
LIB_NAME = libconcord_util.so

# 测试程序
TIMER_DEMO = timer_demo
TIMER_SRCS = timer.c timer_demo.c
TIMER_OBJS = $(TIMER_SRCS:.c=.o)

UTIL_TEST = util_test
UTIL_TEST_SRCS = util_test.c
UTIL_TEST_OBJS = $(UTIL_TEST_SRCS:.c=.o)

.PHONY: all clean test timer-test util-test lib

all: $(LIB_NAME) $(TIMER_DEMO) $(UTIL_TEST)

# 编译共享库
lib: $(LIB_NAME)

$(LIB_NAME): $(UTIL_OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

# 编译定时器测试
$(TIMER_DEMO): $(TIMER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译工具函数测试
$(UTIL_TEST): $(UTIL_TEST_OBJS) $(UTIL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 运行测试
test: util-test timer-test

util-test: $(UTIL_TEST)
	@echo "运行 Util 工具函数测试..."
	./$(UTIL_TEST)

timer-test: $(TIMER_DEMO)
	@echo "运行 Timer 模块测试..."
	timeout 10 ./$(TIMER_DEMO) || true

clean:
	rm -f $(UTIL_OBJS) $(TIMER_OBJS) $(UTIL_TEST_OBJS) $(LIB_NAME) $(TIMER_DEMO) $(UTIL_TEST)

install: $(LIB_NAME)
	sudo cp $(LIB_NAME) /usr/local/lib/
	sudo cp *.h /usr/local/include/ 