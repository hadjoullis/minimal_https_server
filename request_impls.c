#include "request_impls.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern char *HOME;

static int file_exists(const char *path) { return access(path, F_OK); }

static char *extract_filepath(const char *request, const char *type) {
    if (request == NULL) {
        return NULL;
    }

    char *buffer = malloc(strlen(type) + 2); // 1 for space, 1 for \0
    if (buffer == NULL) {
        perror("buffer allocation in extract_filepath");
        return NULL;
    }
    strcpy(buffer, type);
    strcat(buffer, " ");

    const char *start = strstr(request, buffer);
    if (start == NULL) {
        return NULL;
    }
    start += strlen(buffer);
    free(buffer);
    const char *end = strstr(start, " HTTP/");
    if (end == NULL) {
        return NULL;
    }

    size_t length = (end - 1) - start;
    char *filepath = malloc(strlen(HOME) + 1 + length + 1);
    if (filepath == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < strlen(filepath); i++)
        filepath[i] = '\0';

    strcat(filepath, HOME);
    strcat(filepath, "/");
    strncat(filepath, start + 1, length);
    filepath[strlen(HOME) + 1 + length] = '\0';

    return filepath;
}

static int get_file(const char *path, char **content) {
    FILE *fp;
    long lsize;

    fp = fopen(path, "rb");
    if (!fp) {
        perror("file");
        return 0;
    }

    fseek(fp, 0L, SEEK_END);
    lsize = ftell(fp);
    rewind(fp);

    *content = calloc(1, lsize + 1);
    if (!*content) {
        fclose(fp);
        fputs("memory alloc fails", stderr);
        exit(1);
    }

    if (1 != fread(*content, lsize, 1, fp)) {
        fclose(fp);
        free(content);
        fputs("entire read fails", stderr);
        exit(1);
    }

    fclose(fp);
    return 1;
}

int _GET(char *request, char *response, char **content) {
    if (request == NULL || response == NULL) {
        strcpy(response, RESPONSE_NOT_FOUND);
        return 404;
    }
    char *filepath;
    filepath = extract_filepath(request, "GET");
    // fprintf(stderr, "filepath:%s\n", filepath);
    if (file_exists(filepath) == 1) {

        if (get_file(filepath, content) == 1) {
            strcpy(response, RESPONSE_OK);
            return 204;
        } else {
            strcpy(response, RESPONSE_NOT_FOUND);
            return 404;
        }
    } else {
        strcpy(response, RESPONSE_NOT_FOUND);
        return 404;
    }
}

static long file_head(const char *path) {
    FILE *fp;
    long lsize = -1;

    fp = fopen(path, "rb");
    if (!fp) {
        perror("file");
        return 0;
    }

    fseek(fp, 0L, SEEK_END);
    lsize = ftell(fp);
    long *f_size = malloc(sizeof(int *));
    *f_size = lsize;
    return *f_size;
}

int _HEAD(char *request, char *response, long *f_size) {

    if (request == NULL || response == NULL) {
        strcpy(response, RESPONSE_NOT_FOUND);
        return 404;
    }
    char *filepath;
    filepath = extract_filepath(request, "HEAD");
    if (file_exists(filepath) == 1) {
        *f_size = file_head(filepath);
        if (*f_size != -1) {
            strcpy(response, RESPONSE_OK);
            return 204;
        } else {
            strcpy(response, RESPONSE_NOT_FOUND);
            return 404;
        }
    } else {
        strcpy(response, RESPONSE_NOT_FOUND);
        return 404;
    }
}

static int extractcontent(char *request, char **content) {
    if (request == NULL) {
        return 0;
    }
    const char *body_start = strstr(request, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        *content = strdup(body_start);
        return 1;
    } else {
        *content = NULL;
        return 0;
    }
    return 1;
}

static int create_directories_for_file(const char *path) {
    char directories[256] = "";
    strcpy(directories, path);
    char *last_slash = strrchr(directories, '/');
    directories[last_slash - directories] = '\0';

    char command[300] = "mkdir -p ";
    strcat(command, directories);
    // fprintf(stderr, "command:%s\n", command);
    system(command);
    return 0;
}

int _POST(char *request, char *response) {
    char *content;
    if (request == NULL || response == NULL) {
        strcpy(response, RESPONSE_NOT_FOUND);
        return 404;
    }
    char *filepath;
    filepath = extract_filepath(request, "POST");
    int err_val = extractcontent(request, &content);
    if (err_val == 0) {
        perror("extractcontent");
        strcpy(response, RESPONSE_INTERNAL_ERROR);
        return 500;
    }
    if (file_exists(filepath) == 1) {
        remove(filepath);
        FILE *file = fopen(filepath, "w");
        if (file == NULL) {
            perror("file");
            return -1;
        }
        fprintf(file, "%s", content);
        fclose(file);
        strcpy(response, RESPONSE_CREATED);
        return 201;
    } else {
        create_directories_for_file(filepath);
        FILE *file = fopen(filepath, "w");
        if (file == NULL) {
            perror("file");
            return -1;
        }
        fprintf(file, "%s", content);
        fclose(file);
        strcpy(response, RESPONSE_CREATED);
        return 201;
    }
}

static int delete_file(const char *path) {
    if (remove(path) == 0) {
        return 1;
    }
    perror("delete file");
    return 0;
}

int _DELETE(char *request, char *response, char **content) {

    if (request == NULL || response == NULL) {
        strcpy(response, RESPONSE_BAD_REQUEST);
        return 400;
    }

    char *path = extract_filepath(request, "DELETE");
    if (path == NULL) {
        strcpy(response, RESPONSE_BAD_REQUEST);
        return 404;
    }

    if (file_exists(path)) {
        if (delete_file(path) != 0) {
            free(path);
            strcpy(response, RESPONSE_NO_CONTENT);
            return 204;
        } else {
            free(path);
            strcpy(response, RESPONSE_BAD_REQUEST);
            return 404;
        }
    } else {
        *content = (char *)malloc(sizeof("Document not found!" + 1));
        if (*content == NULL) {
            return 500;
            strcpy(response, RESPONSE_INTERNAL_ERROR);
        }
        *content = "Document was not found!";
        strcpy(response, RESPONSE_NOT_FOUND);
        return 404;
    }
}
