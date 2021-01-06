/**
 * Multithreaded, libevent-based socket server.
 * Copyright (c) 2012 Ronald Bennett Cemer
 * Copyright (c) 2020 Florian Fischer
 * This software is licensed under the BSD license.
 * See the accompanying LICENSE.txt for details.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <event.h>
#include <signal.h>

#include "thread.h"

/* Port to listen on. */
#define SERVER_PORT 12345
/* Connection backlog (# of backlogged connections to accept). */
#define CONNECTION_BACKLOG 8
/* Socket read and write timeouts, in seconds. */
#define SOCKET_READ_TIMEOUT_SECONDS 10
#define SOCKET_WRITE_TIMEOUT_SECONDS 10
/* Number of worker threads. -1 will start N threads where N is number of available CPUs */
#define NUM_THREADS -1

/* Behaves similarly to fprintf(stderr, ...), but adds file, line, and function
 information. */
#define errorOut(...) {\
	fprintf(stderr, "%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
	fprintf(stderr, __VA_ARGS__);\
}

#define unlikely(x) __builtin_expect(!!(x), 0)

/**
 * Struct to carry around connection (client)-specific data.
 */
typedef struct client {
	/* The client's socket. */
	int fd;

	/* The bufferedevent for this client. */
	struct bufferevent *buf_ev;

	/* The output buffer for this client. */
	struct evbuffer *output_buffer;

	/* Here you can add your own application-specific attributes which
	 * are connection-specific. */
} client_t;

static struct event_base *evbase_accept;

/**
 * Set a socket to non-blocking mode.
 */
static int setnonblock(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0) return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;
	return 0;
}

static void closeClient(client_t *client) {
	if (client != NULL) {
		if (client->fd >= 0) {
			close(client->fd);
			client->fd = -1;
		}
	}
}

static void closeAndFreeClient(client_t *client) {
	if (client != NULL) {
		closeClient(client);
		if (client->buf_ev != NULL) {
			bufferevent_free(client->buf_ev);
			client->buf_ev = NULL;
		}
		if (client->output_buffer != NULL) {
			evbuffer_free(client->output_buffer);
			client->output_buffer = NULL;
		}
		free(client);
	}
}

static client_t* client_new(int client_fd) {
	client_t* client;

	/* Set the client socket to non-blocking mode. */
	if (setnonblock(client_fd) < 0) {
		warn("failed to set client socket to non-blocking");
		close(client_fd);
		return NULL;
	}

	/* Create a client object. */
	if ((client = malloc(sizeof(*client))) == NULL) {
		warn("failed to allocate memory for client state");
		close(client_fd);
		return NULL;
	}
	memset(client, 0, sizeof(*client));
	client->fd = client_fd;

	if ((client->output_buffer = evbuffer_new()) == NULL) {
		warn("client output buffer allocation failed");
		closeAndFreeClient(client);
		return NULL;
	}

	return client;
}

/**
 * Called by libevent when there is data to read.
 */
void buffered_on_read(struct bufferevent *bev, void *arg) {
	client_t *client = (client_t *)arg;
	struct evbuffer_ptr p;
	evbuffer_ptr_set(bev->input, &p, 0, EVBUFFER_PTR_SET);

	p = evbuffer_search(bev->input, "quit\n", 5, &p);
	if (p.pos != -1) {
		exit(EXIT_SUCCESS);
	}

	int err = evbuffer_add_buffer(bev->input, bev->output);
	if (unlikely(err)) {
		errorOut("Error copying input to output on fd %d\n", client->fd);
	}

	/* Send the results to the client.  This actually only queues the results for sending.
	 * Sending will occur asynchronously, handled by libevent. */
	err = bufferevent_write_buffer(bev, client->output_buffer);
	if (unlikely(err)) {
		errorOut("Error sending data to client on fd %d\n", client->fd);
		closeAndFreeClient(client);
	}
}

/**
 * Called by libevent when the write buffer reaches 0.  We only
 * provide this because libevent expects it, but we don't use it.
 */
void buffered_on_write(struct bufferevent *bev, void *arg) {
}

/**
 * Called by libevent when there is an error on the underlying socket
 * descriptor.
 */
void buffered_on_error(struct bufferevent *bev, short what, void *arg) {
	closeAndFreeClient((client_t *)arg);
}

/* Register the client on the event_base */
void client_serve(client_t* client, struct event_base* evbase) {

	/* Create the buffered event.
	 *
	 * The first argument is the file descriptor that will trigger
	 * the events, in this case the clients socket.
	 *
	 * The second argument is the callback that will be called
	 * when data has been read from the socket and is available to
	 * the application.
	 *
	 * The third argument is a callback to a function that will be
	 * called when the write buffer has reached a low watermark.
	 * That usually means that when the write buffer is 0 length,
	 * this callback will be called.  It must be defined, but you
	 * don't actually have to do anything in this callback.
	 *
	 * The fourth argument is a callback that will be called when
	 * there is a socket error.  This is where you will detect
	 * that the client disconnected or other socket errors.
	 *
	 * The fifth and final argument is to store an argument in
	 * that will be passed to the callbacks.  We store the client
	 * object here.
	 */
	if ((client->buf_ev = bufferevent_new(client->fd,
	                                      buffered_on_read,
	                                      buffered_on_write,
	                                      buffered_on_error,
	                                      client)) == NULL) {
		warn("client bufferevent creation failed");
		closeAndFreeClient(client);
		return;
	}

	bufferevent_base_set(evbase, client->buf_ev);

	bufferevent_settimeout(client->buf_ev,
	                       SOCKET_READ_TIMEOUT_SECONDS,
	                       SOCKET_WRITE_TIMEOUT_SECONDS);

	/* We have to enable it before our callbacks will be
	 * called. */
	bufferevent_enable(client->buf_ev, EV_READ);
}

/**
 * This function will be called by libevent when there is a connection
 * ready to be accepted.
 */
void on_accept(int fd, short ev, void *arg) {
	int client_fd;
	client_t *client;

	client_fd = accept(fd, NULL, NULL);
	if (client_fd < 0) {
		warn("accept failed");
		return;
	}

	client = client_new(client_fd);
	if (client) {
		dispatch_new_client(client);
	}
}

int main(int argc, char *argv[]) {
	int listenfd;
	struct sockaddr_in listen_addr;
	struct event ev_accept;
	int reuseaddr_on;

	/* Initialize libevent. */
	event_init();

	/* Create our listening socket. */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		err(1, "listen failed");
	}
	memset(&listen_addr, 0, sizeof(listen_addr));

	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(SERVER_PORT);

	reuseaddr_on = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, sizeof(reuseaddr_on));

	if (bind(listenfd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
		err(1, "bind failed");
	}

	if (listen(listenfd, CONNECTION_BACKLOG) < 0) {
		err(1, "listen failed");
	}

	/* Set the socket to non-blocking, this is essential in event
	 * based programming with libevent. */
	if (setnonblock(listenfd) < 0) {
		err(1, "failed to set server socket to non-blocking");
	}

	if ((evbase_accept = event_base_new()) == NULL) {
		perror("Unable to create socket accept event base");
		close(listenfd);
		return 1;
	}

	unsigned nthreads;
	if (NUM_THREADS == -1) {
		nthreads = get_nprocs();
	} else {
		nthreads = NUM_THREADS;
	}
	thread_init(nthreads);

	/* We now have a listening socket, we create a read event to
	 * be notified when a client connects. */
	event_set(&ev_accept, listenfd, EV_READ|EV_PERSIST, on_accept, NULL);
	event_base_set(evbase_accept, &ev_accept);
	event_add(&ev_accept, NULL);

	printf("Server listening on %d. Using %d worker threads\n", SERVER_PORT, nthreads);

	/* Start the event loop. */
	event_base_dispatch(evbase_accept);

	event_base_free(evbase_accept);
	evbase_accept = NULL;

	close(listenfd);

	printf("Server shutdown.\n");

	return 0;
}
