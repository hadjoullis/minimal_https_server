#include <arpa/inet.h>
#include <errno.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "request_handler.h"

int THREADS;
int PORT;
char *HOME;

pthread_mutex_t mutex;
pthread_cond_t *cond;
CONN **connections;

void cleanup(SSL *ssl, int client, int *execute) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client);
    *execute = 0;
}

int create_socket(int port) {
    int s;
    struct sockaddr_in addr;

    /* set the type of connection to TCP/IP */
    addr.sin_family = AF_INET;
    /* set the server port number */
    addr.sin_port = htons(port);
    /* set our address to any interface */
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("Unable to create socket");
        exit(EXIT_FAILURE);
    }

    /* bind serv information to s socket */
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }

    /* start listening allowing a queue of up to 1 pending connection */
    if (listen(s, 1) < 0) {
        perror("Unable to listen");
        exit(EXIT_FAILURE);
    }

    return s;
}

void init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl() { EVP_cleanup(); }

SSL_CTX *create_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    /* The actual protocol version used will be negotiated to the
     * highest version mutually supported by the client and the server.
     * The supported protocols are SSLv3, TLSv1, TLSv1.1 and TLSv1.2.
     */
    method = TLS_server_method();

    /* creates a new SSL_CTX object as framework to establish TLS/SSL or
     * DTLS enabled connections. It initializes the list of ciphers, the
     * session cache setting, the callbacks, the keys and certificates,
     * and the options to its default values
     */
    ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    return ctx;
}

void configure_context(SSL_CTX *ctx) {
    SSL_CTX_set_ecdh_auto(ctx, 1);

    /* Set the key and cert using dedicated pem files */
    if (SSL_CTX_use_certificate_file(ctx, "cert.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "key.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
}

int configure_server() {
    FILE *fp = fopen("config.txt", "r");
    if (fp == NULL) {
        perror("config.txt");
        return EXIT_FAILURE;
    }

    char buffer[BUF_SIZE] = "";
    char *token;

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        if (buffer[0] == '#' || buffer[0] == '\n')
            continue;
        buffer[strcspn(buffer, "\n")] = '\0';
        if (strstr(buffer, "THREADS") != NULL) {
            token = strtok(buffer, "=");
            token = strtok(NULL, "=");
            THREADS = atoi(token);
        } else if (strstr(buffer, "PORT") != NULL) {
            token = strtok(buffer, "=");
            token = strtok(NULL, "=");
            PORT = atoi(token);
        } else if (strstr(buffer, "HOME") != NULL) {
            token = strtok(buffer, "=");
            token = strtok(NULL, "=");
            HOME = (char *)malloc(strlen(token) + 1);
            if (HOME == NULL) {
                perror("HOME path");
                fclose(fp);
                return EXIT_FAILURE;
            }
            strcpy(HOME, token);
        }
    }

    fclose(fp);
    return EXIT_SUCCESS;
}

int main(void) {
    if (configure_server() == EXIT_FAILURE)
        return EXIT_FAILURE;

    int sock;
    SSL_CTX *ctx;

    /* initialize OpenSSL */
    init_openssl();

    /* setting up algorithms needed by TLS */
    ctx = create_context();

    /* specify the certificate and private key to use */
    configure_context(ctx);

    sock = create_socket(PORT);

    cond = malloc(sizeof(pthread_cond_t) * THREADS);
    connections = malloc(sizeof(CONN *) * THREADS);
    pthread_t *tid = malloc(sizeof(pthread_t) * THREADS);
    int *args = malloc(sizeof(int) * THREADS);

    if (cond == NULL || connections == NULL || args == NULL || tid == NULL) {
        if (cond == NULL)
            perror("cond");
        if (connections == NULL)
            perror("connections");
        if (args == NULL)
            perror("args");
        if (tid == NULL)
            perror("tid");

        close(sock);
        SSL_CTX_free(ctx);
        cleanup_openssl();
        return EXIT_FAILURE;
    }

    int i, err, execute = 1;
    for (i = 0; i < THREADS; i++) {
        connections[i] = NULL;
        args[i] = i;
        if ((err = pthread_create(&(tid[i]), NULL, (void *)&request_handler,
                                  (void *)&(args[i])))) {
            perror_thread("pthread_create", err);
            execute = 0;
            break;
        }
        pthread_cond_init(&cond[i], NULL);
    }
    pthread_mutex_init(&mutex, NULL);
    free(args);

    int current_thread = 0;

    /* Handle connections */
    while (execute) {
        printf("Accepting client....\n");
        struct sockaddr_in addr;
        uint len = sizeof(addr);
        SSL *ssl;

        /* Server accepts a new connection on a socket.
         * Server extracts the first connection on the queue
         * of pending connections, create a new socket with the same
         * socket type protocol and address family as the specified
         * socket, and allocate a new file descriptor for that socket.
         */
        int client = accept(sock, (struct sockaddr *)&addr, &len);
        if (client < 0) {
            perror("Unable to accept");
            exit(EXIT_FAILURE);
        }

        /* creates a new SSL structure which is needed to hold the data
         * for a TLS/SSL connection
         */
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client);

        /* wait for a TLS/SSL client to initiate a TLS/SSL handshake */
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
        }
        /* if TLS/SSL handshake was successfully completed, a TLS/SSL
         * connection has been established
         */
        else {
            CONN *connection = (CONN *)malloc(sizeof(CONN));
            if (connection == NULL) {
                perror("connection");
                SSL_shutdown(ssl);
                SSL_free(ssl);
                close(client);
                break;
            }

            connection->ssl = ssl;
            connection->socket = client;

            if ((err = pthread_mutex_lock(&mutex))) {
                perror_thread("pthread_mutex_lock", err);
                cleanup(ssl, client, &execute);
                break;
            }

            connections[current_thread] = connection;
            pthread_cond_signal(&cond[current_thread]);

            if ((err = pthread_mutex_unlock(&mutex))) {
                perror_thread("pthread_mutex_unlock", err);
                cleanup(ssl, client, &execute);
                break;
            }

            current_thread++;
            if (current_thread == THREADS) {
                current_thread = 0;
            }
        }
    }

    for (i = 0; i < THREADS; i++) {
        pthread_cancel(tid[i]);
        pthread_cond_destroy(&cond[i]);
    }
    pthread_mutex_destroy(&mutex);

    free(tid);
    free(connections);

    close(sock);
    SSL_CTX_free(ctx);
    cleanup_openssl();

    if (execute == 0)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
