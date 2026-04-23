#ifndef BUFFER_H
#define BUFFER_H

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/sendfile.h>

#include "defs.h"

struct buffer {
  char *data;
  size_t len;
  size_t cap;
};

bool buffer_init(struct buffer *buf, size_t cap);

void buffer_free(struct buffer *buf);

void buffer_consume(struct buffer *buf, size_t n);

int buffer_append(struct buffer *buf, const void *src, size_t n);

int buffer_send_flat(int fd, struct buffer *buf, size_t *sent);

int buffer_send_file(int sock_fd, int file_fd, off_t *offset, size_t file_size);

#endif
