# Multithreaded, libevent socket server based on code from Ronald Bennett Cemer and memcached.

This software is licensed under the BSD license.
See the accompanying LICENSE.txt for details.

To compile: `make`

To run: `./echoserver`

# Original from Ronald Cemer

Ronald Cemer's blogpost can be found here: https://roncemer.com/software-development/multi-threaded-libevent-server-example/

Original source code is located here: https://sourceforge.net/projects/libevent-thread/

## Original description from Ronald Cemer

Libevent is a nice library for handling and dispatching events, as well as doing nonblocking I/O.  This is fine, except that it is basically single-threaded -- which means that if you have multiple CPUs or a CPU with hyperthreading, you're really under-utilizing the CPU resources available to your server application because your event pump is running in a single thread and therefore can only use one CPU core at a time.

The solution is to create one libevent event queues (AKA event_base) per active connection, each with its own event pump thread.  This project does exactly that, giving you everything you need to write high-performance, multi-threaded, libevent-based socket servers.

There are mentionings of running libevent in a multithreaded implementation, however it is very difficult (if not impossible) to find working implementations.  This project is a working implementation of a multi-threaded, libevent-based socket server.

The server itself simply echoes whatever you send to it.  Start it up, then telnet to it:
    telnet localhost 5555
Everything you type should be echoed back to you.

The implementation is fairly standard.  The main thread listens on a socket and accepts new connections, then farms the actual handling of those connections out to a pool of worker threads.  Each connection has its own isolated event queue.

In theory, for maximum performance, the number of worker threads should be set to the number of CPU cores available.  Feel free to experiment with this.

Also note that the server includes a multithreaded work queue implementation, which can be re-used for other purposes.

Since the code is BSD licensed, you are free to use the source code however you wish, either in whole or in part.

Some inspiration and coding ideas came from echoserver and cliserver, both of which are single-threaded, libevent-based servers.

Echoserver is located here: http://ishbits.googlecode.com/svn/trunk/libevent-examples/echo-server/libevent_echosrv1.c
Cliserver is located here: http://nitrogen.posterous.com/cliserver-an-example-libevent-based-socket-se
# Flaws of the original design and improvements

The original design of assigning each connection its own event_base is multithreaded
but not in the way you would expect from a server.

Calling event_base_dispatch per connection on a worker thread results in
an event loop only handling events for __one__ file descriptor per worker thread.

This means only N connections can be served in parallel where N is the amount of worker threads.
Clearly this is not how libevent or any event-loop is meant to be used.

## event_base and event_base_dispatch

An event_base is an OS independent event-loop abtraction.
Events can be registered on a event_base and their callbacks will be executed
by the thread running the event_base's event loop by calling event_base_dispatch.

## memcached's worker thread design

To support multiple connections per worker all sockets are worker is responsible for
must be added to its event_base.
New connections are accepted by the main thread running its on event loop.
Sockets created in the `on_accept` callback are dispatched round-robin to
the worker threads.
Each worker thread has a pipe through which new connection can be passed.
When a worker thread receives a event for its new connection pipe it will
pop a new item from the item queue and register the client on its event_base.

Therefore connections are spread somewhat evenly among all worker thread
which can handle an arbitrary amount of connections at once.
