CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC -std=c99 -g
LDFLAGS = -shared

# 源文件
SOURCES = config.c parser.c
OBJECTS = $(SOURCES:.c=.o)
LIB_NAME = libconcord_config.so
TEST_SOURCES = config_test.c
TEST_BINARY = config_test

.PHONY: all clean test

all: $(LIB_NAME) $(TEST_BINARY)

$(LIB_NAME): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TEST_BINARY): $(TEST_SOURCES) $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BINARY)
	./$(TEST_BINARY)

clean:
	rm -f $(OBJECTS) $(LIB_NAME) $(TEST_BINARY)

install: $(LIB_NAME)
	sudo cp $(LIB_NAME) /usr/local/lib/
	sudo cp *.h /usr/local/include/ 