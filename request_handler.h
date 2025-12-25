#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>

#define perror_thread(s, e) (fprintf(stderr, "%s: %s\n", s, strerror(e)))
#define BUF_SIZE 2048

typedef struct {
    int socket;
    SSL *ssl;
} CONN;

enum request_types { NONE = -1, GET = 0, HEAD = 1, POST = 2, DELETE = 3 };

extern pthread_mutex_t mutex;
extern pthread_cond_t *cond;
extern CONN **connections;

void *request_handler(void *arg);
void cleanup_noexit(CONN *conn);
void cleanup_exit(CONN *conn);

#endif
