CC ?=gcc
CFLAGS ?=-Wall -O3
LDFLAGS ?=-levent -pthread

all: echoserver

echoserver: echoserver.c thread.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
