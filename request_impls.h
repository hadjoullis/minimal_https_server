#ifndef REQUEST_IMPLS_H
#define REQUEST_IMPLS_H

extern char *HOME;

#define RESPONSE_OK "200 OK"
#define RESPONSE_CREATED "201 Created"
#define RESPONSE_NO_CONTENT "204 No Content"
#define RESPONSE_BAD_REQUEST "400 Bad Request"
#define RESPONSE_NOT_FOUND "404 Not Found"
#define RESPONSE_INTERNAL_ERROR "500 Internal Server Error"

int _GET(char *request, char *response, char **content);
int _HEAD(char *request, char *response, long *f_size);
int _POST(char *request, char *response);
int _DELETE(char *request, char *response, char **content);

#endif
