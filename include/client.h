#ifndef CLIENT_H
#define CLIENT_H

#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>

#include "defs.h"
#include "buffer.h"
#include "backend.h"
#include "fd_ctx.h"

enum body_kind {
  BODY_NONE,
  BODY_BUFFER,
  BODY_FILE
};

struct client_state {
  int fd;
  bool in_has_body;
  bool in_url_is_static;
  struct buffer in_headers;
  struct buffer in_body;
  struct buffer out_headers;
  size_t out_headers_sent;
  struct buffer out_body;
  size_t out_body_sent;
  enum body_kind out_body_kind;
  int out_file_fd;
  size_t out_file_size;
  off_t out_file_offset;
  struct backend_state backend;
  struct fd_ctx client_ctx;
  struct fd_ctx backend_ctx;
  enum {
    CLIENT_READING_HEADERS,
    CLIENT_READING_BODY,
    CLIENT_WRITING_HEADERS,
    CLIENT_WRITING_BODY,
    CLIENT_CLOSING,
    CLIENT_IDLE
  } state;
};

void client_accept_conn(int epfd, int server_fd);

void client_close_and_free(int epfd, struct client_state *client);

void client_handle_read(int epfd, struct client_state *client);

void client_handle_write(int epfd, struct client_state *client);

#endif



