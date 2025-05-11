# 编译器和选项
CC = gcc
CFLAGS = -Wall -g -O2 -fPIC
LDFLAGS = -lpthread

# 目标文件
OBJS = kvstore.o kvstore_array.o kvstore_hash.o kvstore_rbtree.o \
       kv_persist.o kv_concurrency.o kv_transaction.o kv_error.o kv_cluster.o

# 目标可执行文件
TARGET = kvserver
SHARED_LIB = libkvstore.so

# 默认目标
all: $(TARGET) $(SHARED_LIB)

# 静态编译可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译共享库
$(SHARED_LIB): $(OBJS)
	$(CC) -shared $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 规则：从.c文件生成.o文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(TARGET) $(SHARED_LIB) *.o

# 安装
install: $(TARGET) $(SHARED_LIB)
	mkdir -p $(DESTDIR)/usr/local/bin
	mkdir -p $(DESTDIR)/usr/local/lib
	cp $(TARGET) $(DESTDIR)/usr/local/bin/
	cp $(SHARED_LIB) $(DESTDIR)/usr/local/lib/
	ldconfig

# 卸载
uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
	rm -f $(DESTDIR)/usr/local/lib/$(SHARED_LIB)
	ldconfig

# 声明伪目标
.PHONY: all clean install uninstall
