CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -pthread
LDFLAGS = -pthread -lm

METRICS_SRCS = metrics.c metrics_format.c metrics_demo.c
METRICS_OBJS = $(METRICS_SRCS:.c=.o)
METRICS_DEMO = metrics_demo

.PHONY: all clean

all: $(METRICS_DEMO)

$(METRICS_DEMO): $(METRICS_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(METRICS_OBJS) $(METRICS_DEMO) 