CC ?=gcc
CFLAGS ?=-Wall
LDFLAGS ?=-levent -pthread

all: echoserver

echoserver: echoserver.c thread.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
