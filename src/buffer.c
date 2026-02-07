#include <stdlib.h>
#include <string.h>

#include "buffer.h"

bool buffer_init(struct buffer *buf, size_t cap) {
  buf->data = cap ? malloc(cap) : NULL;
  if (cap && !buf->data) return false;
  buf->len = 0;
  buf->cap = cap;
  return true;
}

void buffer_free(struct buffer *buf) {
  free(buf->data);
  *buf = (struct buffer){0};
}

void buffer_consume(struct buffer *buf, size_t n) {
  if (n == 0) return;
  if (n >= buf->len) {
    buf->len = 0;
    return;
  }
  memmove(buf->data, buf->data + n, buf->len - n);
  buf->len -= n;
}

int buffer_append(struct buffer *buf, const void *src, size_t n) {
  if (n == 0) return 0;
  if (buf->len + n > buf->cap) {
    size_t new_cap = buf->cap ? buf->cap : 1;
    while (new_cap < buf->len + n) new_cap *= 2;
    char *new_ptr = realloc(buf->data, new_cap);
    if (!new_ptr)
      return -1;
    buf->data = new_ptr;
    buf->cap  = new_cap;
  }
  memcpy(buf->data + buf->len, src, n);
  buf->len += n;
  return 0;
}


