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

struct client_state;

struct backend_state {
  int fd;
  bool in_has_body;
  struct client_state *client;
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

void backend_init(struct backend_state *backend);

void backend_destroy(int epfd, struct backend_state *backend);

int backend_connect(const char *ip, uint16_t port);

bool backend_epoll_register(int epfd, int fd, struct client_state *client);

void backend_epoll_toggle_write(
  int epfd,
  struct backend_state *backend,
  bool add_write
);

void backend_detach_client(
  struct client_state *client,
  struct backend_state *backend
);

int backend_handle_err(int epfd, struct backend_state *backend);

int backend_handle_read(int epfd, struct backend_state *backend);

int backend_handle_write(int epfd, struct backend_state *backend);

#endif

