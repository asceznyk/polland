#ifndef BACKEND_H
#define BACKEND_H

#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "defs.h"
#include "fd_ctx.h"

typedef struct client_t client_t;
typedef struct backend_t backend_t;

extern int backend_entry_count;

struct backend_t {
  int fd;
  bool closing;
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
  int ridx;
  backend_t *next;
  backend_t *prev;
  backend_t *next_to_free;
};

extern backend_t *backend_pool;

extern backend_t *backend_registry[MAX_UPSTREAM_CONNECTIONS];

void backend_show_registry();

backend_t *backend_create(int fd);

backend_t *backend_build_pool(int epfd, int k);

backend_t *backend_detach_from_pool();

void backend_attach_to_pool(backend_t *entry);

void backend_destroy(int epfd, backend_t *backend);

int backend_connect(const char *ip, uint16_t port);

bool backend_epoll_register(int epfd, backend_t *backend);

void backend_epoll_toggle_write(
  int epfd,
  backend_t *backend,
  bool add_write
);

void backend_attach_client(
  client_t *client,
  backend_t *backend_t
);

void backend_detach_client(
  client_t *client,
  backend_t *backend
);

int backend_handle_err(int epfd, backend_t *backend);

int backend_handle_read(int epfd, backend_t *backend);

int backend_handle_write(int epfd, backend_t *backend);

#endif
