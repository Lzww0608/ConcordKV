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
BENCHMARKS = persistence_benchmark

.PHONY: all clean benchmark

all: $(BENCHMARKS)

persistence_benchmark: persistence_benchmark.c $(ENHANCED_OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

benchmark: $(BENCHMARKS)
	@echo "运行性能基准测试（可能需要几分钟）..."
	@echo "⚠️  请确保系统负载较低以获得准确的性能数据"
	@./persistence_benchmark

# 快速基准测试（减少操作数量）
quick-benchmark: CFLAGS += -DNUM_OPERATIONS=10000
quick-benchmark: $(BENCHMARKS)
	@echo "运行快速基准测试..."
	@./persistence_benchmark

clean:
	rm -f $(BENCHMARKS) $(ENHANCED_OBJECTS)
	rm -rf /tmp/benchmark_*

help:
	@echo "可用的目标:"
	@echo "  all              - 编译所有性能测试"
	@echo "  benchmark        - 运行完整性能基准测试"
	@echo "  quick-benchmark  - 运行快速基准测试"
	@echo "  clean            - 清理编译文件"
	@echo "  help             - 显示此帮助信息" 