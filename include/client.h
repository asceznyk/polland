#ifndef CLIENT_H
#define CLIENT_H

#include <assert.h>
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
#include "fd_ctx.h"

enum body_kind {
  BODY_NONE,
  BODY_BUFFER,
  BODY_FILE
};

struct backend_state;

struct client_state {
  int fd;
  bool closing;
  struct client_state *next_to_free;
  bool in_has_body;
  bool in_url_is_static;
  bool is_http_one_point_o;
  size_t in_header_end;
  struct buffer in_stream;
  struct buffer out_stream;
  size_t out_sent;
  enum body_kind out_body_kind;
  int out_file_fd;
  size_t out_file_size;
  off_t out_file_offset;
  struct backend_state *backend;
  struct fd_ctx ctx;
  enum {
    CLIENT_READING_HEADERS,
    CLIENT_READING_BODY,
    CLIENT_REQ_COMPLETE
  } in_state;
  enum  {
    CLIENT_WRITING_HEADERS,
    CLIENT_WRITING_BODY,
    CLIENT_RESP_COMPLETE
  } out_state;
};

void client_accept_conn(int epfd, int server_fd);

void client_mark_closing(struct client_state *client);

void client_destroy(int epfd, struct client_state *client);

void client_epoll_toggle_write(
  int epfd, struct client_state *client, bool add_write
);

void client_http_adv_state(struct client_state *client);

int client_handle_read(int epfd, struct client_state *client);

int client_handle_write(int epfd, struct client_state *client);

#endif



