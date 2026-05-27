#ifndef BACKEND_H
#define BACKEND_H

#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "fd_ctx.h"

typedef struct client_t client_t;
typedef struct backend_t backend_t;

struct backend_t {
  int fd;
  bool in_has_body;
  client_t *client;
  struct fd_ctx ctx;
  enum {
    BE_CONNECTING,
    BE_WRITING_HEADERS,
    BE_WRITING_BODY,
    BE_REQ_COMPLETE
  } in_state;
  enum {
    BE_READING_HEADERS,
    BE_READING_BODY,
    BE_RESP_COMPLETE
  } out_state;
  size_t in_sent;
};

void backend_init(backend_t *backend);

void backend_destroy(int epfd, backend_t *backend);

int backend_connect(const char *ip, uint16_t port);

bool backend_epoll_register(int epfd, int fd, client_t *client);

void backend_epoll_toggle_write(
  int epfd,
  backend_t *backend,
  bool add_write
);

void backend_detach_client(
  client_t *client,
  backend_t *backend
);

int backend_handle_err(int epfd, backend_t *backend);

int backend_handle_read(int epfd, backend_t *backend);

int backend_handle_write(int epfd, backend_t *backend);

#endif
