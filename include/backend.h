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

struct client_state;

struct backend_state {
  int fd;
  enum {
    BE_CONNECTING,
    BE_READING_HEADERS,
    BE_READING_BODY,
    BE_WRITING_HEADERS,
    BE_WRITING_BODY,
    BE_DONE
  } state;
  size_t in_headers_sent;
  size_t in_body_sent;
};

void backend_init(struct backend_state *backend);

void backend_close(int epfd, struct backend_state *backend);

int backend_connect(struct backend_state *backend, const char *ip, uint16_t port);

bool backend_epoll_register(int epfd, struct client_state *client);

int backend_handle_err(int epfd, struct client_state *client);

int backend_handle_read(int epfd, struct client_state *client);

int backend_handle_write(int epfd, struct client_state *client);

#endif
