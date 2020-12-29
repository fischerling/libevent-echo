#ifndef THREAD_H
#define THREAD_H
typedef struct client client_t;
void dispatch_new_client(client_t* client);
void thread_init(int num_threads);
#endif
