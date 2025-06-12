CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lpthread -lm

# 目录
KVSERVER_DIR = ../kvserver
EXAMPLES_DIR = .

# 源文件
CACHE_SOURCES = $(KVSERVER_DIR)/kv_cache.c
DEMO_SOURCES = cache_policies_demo.c

# 目标文件
CACHE_OBJECTS = $(CACHE_SOURCES:.c=.o)
DEMO_OBJECTS = $(DEMO_SOURCES:.c=.o)

# 可执行文件
DEMO_TARGET = cache_policies_demo

# 默认目标
all: $(DEMO_TARGET)

# 编译演示程序
$(DEMO_TARGET): $(DEMO_OBJECTS) $(CACHE_OBJECTS)
	@echo "🔗 链接 $@..."
	$(CC) $(DEMO_OBJECTS) $(CACHE_OBJECTS) -o $@ $(LDFLAGS)
	@echo "✅ 编译完成: $@"

# 编译C文件
%.o: %.c
	@echo "🔨 编译 $<..."
	$(CC) $(CFLAGS) -I$(KVSERVER_DIR) -c $< -o $@

# 运行演示
run: $(DEMO_TARGET)
	@echo "🚀 运行缓存策略演示程序..."
	@echo ""
	./$(DEMO_TARGET)

# 调试运行
debug: $(DEMO_TARGET)
	@echo "🐛 调试模式运行..."
	gdb ./$(DEMO_TARGET)

# 内存检查
valgrind: $(DEMO_TARGET)
	@echo "🔍 内存检查..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(DEMO_TARGET)

# 性能分析
perf: $(DEMO_TARGET)
	@echo "📊 性能分析..."
	perf record -g ./$(DEMO_TARGET)
	perf report

# 清理
clean:
	@echo "🧹 清理编译文件..."
	rm -f $(DEMO_OBJECTS) $(CACHE_OBJECTS) $(DEMO_TARGET)
	rm -f perf.data perf.data.old
	@echo "✅ 清理完成"

# 重新编译
rebuild: clean all

# 检查代码风格
check:
	@echo "🔍 检查代码风格..."
	cppcheck --enable=all --std=c99 $(DEMO_SOURCES) $(CACHE_SOURCES)

# 格式化代码
format:
	@echo "🎨 格式化代码..."
	clang-format -i $(DEMO_SOURCES)

# 安装依赖（Ubuntu/Debian）
install-deps:
	@echo "📦 安装依赖..."
	sudo apt-get update
	sudo apt-get install -y build-essential gcc gdb valgrind cppcheck clang-format

# 帮助信息
help:
	@echo "可用的make目标："
	@echo "  all        - 编译所有程序（默认）"
	@echo "  run        - 编译并运行演示程序"
	@echo "  debug      - 使用gdb调试程序"
	@echo "  valgrind   - 使用valgrind检查内存"
	@echo "  perf       - 性能分析"
	@echo "  clean      - 清理编译文件"
	@echo "  rebuild    - 重新编译"
	@echo "  check      - 代码风格检查"
	@echo "  format     - 格式化代码"
	@echo "  install-deps - 安装开发依赖"
	@echo "  help       - 显示此帮助信息"

# 声明伪目标
.PHONY: all run debug valgrind perf clean rebuild check format install-deps help

# 显示编译信息
info:
	@echo "编译信息："
	@echo "  CC: $(CC)"
	@echo "  CFLAGS: $(CFLAGS)"
	@echo "  LDFLAGS: $(LDFLAGS)"
	@echo "  源文件: $(CACHE_SOURCES) $(DEMO_SOURCES)"
	@echo "  目标文件: $(DEMO_TARGET)" 