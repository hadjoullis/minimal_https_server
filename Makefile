CC = gcc
CFLAGS = -Wall -Wextra
LIBS = -lssl -lcrypto -lpthread

SRC_FILES = tls_server.c request_handler.c request_impls.c
OBJ_FILES = $(SRC_FILES:.c=.o)

TARGET = tls_server.out

$(TARGET): $(OBJ_FILES)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

debug: CFLAGS += -ggdb3
debug: $(TARGET)

clean:
	rm -f $(OBJ_FILES) $(TARGET)
