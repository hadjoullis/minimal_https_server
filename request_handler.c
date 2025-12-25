#include "request_handler.h"
#include "request_impls.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *not_implemented =
    "HTTP/1.1 501 Not Implemented\r\nServer: my_webserver.com\r\n\
Connection: close\r\nContent-Type: text/plain\r\n\
Content-Length: 23\r\n\r\nMethod not implemented!";

void cleanup_noexit(CONN *conn) {
    SSL_shutdown(conn->ssl);
    SSL_free(conn->ssl);
    close(conn->socket);
    free(conn);
}

void cleanup_exit(CONN *conn) {
    SSL_shutdown(conn->ssl);
    SSL_free(conn->ssl);
    close(conn->socket);
    free(conn);
    pthread_exit((void *)EXIT_FAILURE);
}

void check_connection_type(char *request, int *keep_alive) {
    // header field for connection
    char *ka = strstr(request, "Connection: keep-alive");
    char *cl = strstr(request, "Connection: close");

    if (ka != NULL && ka < strstr(request, "\r\n\r\n"))
        *keep_alive = 1;
    else if (cl != NULL && cl < strstr(request, "\r\n\r\n"))
        *keep_alive = 0;
    else // if no connection header field given, assume close
        *keep_alive = 0;
}

enum request_types check_request_type(char *request) {
    char request_line[sizeof("DELETE")] = "";
    strncpy(request_line, request, sizeof("DELETE"));
    char *saveptr = NULL;
    char *token = strtok_r(request_line, " ", &saveptr);

    if (strcmp(token, "GET") == 0)
        return GET;
    if (strcmp(token, "HEAD") == 0)
        return HEAD;
    if (strcmp(token, "POST") == 0)
        return POST;
    if (strcmp(token, "DELETE") == 0)
        return DELETE;
    return NONE;
}

void generate_headers(char *headers, int keep_alive, char *content,
                      enum request_types rt, int found, char *request,
                      long file_size) {
    // 22 for conversion int to str + \r\n
    char itoa[sizeof("Content-Length: ") + 22] = "";
    if (rt == HEAD)
        sprintf(itoa, "Content-Length: %ld\r\n", file_size);
    else if (content)
        sprintf(itoa, "Content-Length: %ld\r\n", strlen(content));
    else
        sprintf(itoa, "Content-Length: 0\r\n");

    strcat(headers, itoa);

    if (keep_alive)
        strcat(headers, "Connection: keep-alive\r\n");
    else
        strcat(headers, "Connection: close\r\n");

    if (rt != GET || found == 404) {
        strcat(headers, "Content-Type: text/plain");
        return;
    }

    // get file path
    char *saveptr = NULL;
    char *token = strtok_r(request, " ", &saveptr);
    token = strtok_r(NULL, " ", &saveptr);
    char *extension = strrchr(token, '.');
    if (extension == NULL) {
        strcat(headers, "Content-Type: text/plain");
        return;
    }
    // skip the '.' to avoid repetition
    extension++;

    if (strcmp(extension, "txt") == 0 || strcmp(extension, "sed") == 0 ||
        strcmp(extension, "awk") == 0 || strcmp(extension, "c") == 0 ||
        strcmp(extension, "h") == 0)
        strcat(headers, "Content-Type: text/plain");
    else if (strcmp(extension, "html") == 0 || strcmp(extension, "htm"))
        strcat(headers, "Content-Type: text/html");
    else if (strcmp(extension, "jpeg") == 0 || strcmp(extension, "jpg"))
        strcat(headers, "Content-Type: image/jpeg");
    else if (strcmp(extension, "gif") == 0)
        strcat(headers, "Content-Type: image/gif");
    else
        strcat(headers, "Content-Type: application/octet-stream");
}

void *request_handler(void *arg) {
    int index = *(int *)arg;
    int err;
    if ((err = pthread_detach(pthread_self()))) {
        perror_thread("pthread_detach", err);
        pthread_exit((void *)EXIT_FAILURE);
    }

    CONN *conn;
    int bytes;
    char request[BUF_SIZE];
    while (1) {
        if ((err = pthread_mutex_lock(&mutex))) {
            perror_thread("pthread_mutex_lock", err);
            cleanup_exit(conn);
        }

        while (connections[index] == NULL) {
            if ((err = pthread_cond_wait(&cond[index], &mutex))) {
                perror_thread("pthread_cont_wait", err);
                cleanup_noexit(conn);
            }
        }

        conn = connections[index];
        connections[index] = NULL;

        if ((err = pthread_mutex_unlock(&mutex))) {
            perror_thread("pthread_mutex_unlock", err);
            cleanup_exit(conn);
        }

        char *crlf = "\r\n";
        int keep_alive = 0;
        do {
            char response[3 * BUF_SIZE] = "";
            char response_status[BUF_SIZE] = "";
            char headers[BUF_SIZE] = "Server: our_server.com\r\n";
            char *content = NULL;

            /* get request */
            bytes = SSL_read(conn->ssl, request, sizeof(request));
            if (bytes > 0) {
                request[bytes] = 0;
                check_connection_type(request, &keep_alive);
                enum request_types rt;
                int found = -1;
                long file_size = 0;

                switch (rt = check_request_type(request)) {
                case GET:
                    found = _GET(request, response_status, &content);
                    break;
                case HEAD:
                    found = _HEAD(request, response_status, &file_size);
                    break;
                case POST:
                    _POST(request, response_status);
                    break;
                case DELETE:
                    _DELETE(request, response_status, &content);
                    break;
                case NONE:
                    SSL_write(conn->ssl, not_implemented,
                              strlen(not_implemented));
                    if (keep_alive)
                        continue;
                    if (!keep_alive)
                        goto exit_loop;
                }
                // fprintf(stderr, "request:\n%s\n", request);
                generate_headers(headers, keep_alive, content, rt, found,
                                 request, file_size);

                strcat(response, "HTTP/1.1 ");
                strcat(response, response_status);
                strcat(response, crlf);
                strcat(response, headers);
                strcat(response, crlf);
                strcat(response, crlf);
                if (content != NULL) {
                    strcat(response, content);
                    free(content);
                }

                SSL_write(conn->ssl, response, strlen(response));
            } else {
                ERR_print_errors_fp(stderr);
            }
        } while (keep_alive);
    exit_loop:

        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
        close(conn->socket);
        free(conn);
    }

    pthread_exit((void *)EXIT_SUCCESS);
}
