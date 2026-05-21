#ifndef FD_CTX_H
#define FD_CTX_H

struct fd_ctx {
  bool closing;
  enum {
    FD_CLIENT,
    FD_BACKEND
  } kind;
  //struct client_state *client;
  void *peer;
};

#endif

