CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -pthread
LDFLAGS = -pthread -lm

TESTING_SRCS = mock_client.c fault_inject.c benchmark.c test_data.c testing_demo.c
TESTING_OBJS = $(TESTING_SRCS:.c=.o)
TESTING_DEMO = testing_demo

.PHONY: all clean

all: $(TESTING_DEMO)

$(TESTING_DEMO): $(TESTING_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TESTING_OBJS) $(TESTING_DEMO) 