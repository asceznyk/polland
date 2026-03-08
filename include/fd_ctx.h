#ifndef FD_CTX_H
#define FD_CTX_H

enum fd_kind {
  FD_CLIENT,
  FD_BACKEND
};

struct fd_ctx {
  bool closing;
  enum fd_kind kind;
  struct client_state *client;
};

#endif

