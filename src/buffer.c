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

int buffer_send_flat(
  int fd, struct buffer *buf, size_t *sent
) {
  while (*sent < buf->len) {
    size_t remaining = buf->len - *sent;
    size_t to_send = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
    ssize_t n = send(fd, buf->data + *sent, to_send, 0);
    if (n > 0) {
      *sent += n;
      continue;
    }
    if (n == 0) return -1;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    printf("buffer_send_flat: failed?\n");
    return -1;
  }
  return 1;
}

int buffer_send_file(
  int sock_fd,
  int file_fd,
  off_t *offset,
  size_t file_size
) {
  while (*offset < (off_t)file_size) {
    size_t remaining = file_size - *offset;
    size_t to_send = remaining < FILE_CHUNK_SIZE ? remaining : FILE_CHUNK_SIZE;
    ssize_t n = sendfile(sock_fd, file_fd, offset, to_send);
    if (n > 0) continue;
    if (n == 0) {
      if (*offset >= (off_t)file_size)
        break;
      return -1;
    }
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -1;
  }
  return 1;
}

