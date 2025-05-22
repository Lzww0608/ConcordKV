# timer_demo 测试
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -pthread
LDFLAGS = -pthread -lrt

TIMER_SRCS = timer.c timer_demo.c
TIMER_OBJS = $(TIMER_SRCS:.c=.o)
TIMER_DEMO = timer_demo

.PHONY: all clean

all: $(TIMER_DEMO)

$(TIMER_DEMO): $(TIMER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TIMER_OBJS) $(TIMER_DEMO) 