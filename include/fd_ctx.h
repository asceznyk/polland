#ifndef FD_CTX_H
#define FD_CTX_H

typedef struct {
  void *peer;
  enum {
    FD_CLIENT,
    FD_BACKEND
  } kind;
  int fd;
  bool closing;
} fd_ctx_t;

#endif
