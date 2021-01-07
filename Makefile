CC ?=gcc
CFLAGS ?=-Wall -O3
LDFLAGS ?=-levent -pthread

.PHONY: all clean

all: echoserver

clean:
	rm -f echoserver

echoserver: echoserver.c thread.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
