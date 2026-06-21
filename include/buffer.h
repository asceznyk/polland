#ifndef BUFFER_H
#define BUFFER_H

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/sendfile.h>

typedef struct buffer_t buffer_t;

struct buffer_t {
  char *data;
  size_t len;
  size_t cap;
};

bool buffer_init(buffer_t *buf, size_t cap);

void buffer_free(buffer_t *buf);

void buffer_consume(buffer_t *buf, size_t n);

int buffer_append(buffer_t *buf, const void *src, size_t n);

int buffer_send_flat(int fd, buffer_t *buf, size_t len, size_t *sent);

int buffer_send_file(int sock_fd, int file_fd, off_t *offset, size_t file_size);

#endif
