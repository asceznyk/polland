#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdbool.h>

struct buffer {
  char *data;
  size_t len;
  size_t cap;
};

bool buffer_init(struct buffer *buf, size_t cap);

void buffer_free(struct buffer *buf);

void buffer_consume(struct buffer *buf, size_t n);

int buffer_append(struct buffer *buf, const void *src, size_t n);

#endif
