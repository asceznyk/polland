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
#include "transaction.h"
#include "fd_ctx.h"

enum body_kind {
  BODY_NONE,
  BODY_BUFFER,
  BODY_FILE
};

typedef struct backend_t backend_t;
typedef struct client_t client_t;

extern int client_req_count;

struct client_t {
  transaction_t transaction;
  buffer_t in_stream;
  buffer_t out_stream;
  backend_t *backend;
  client_t *free_head;
  size_t out_sent;
  size_t out_file_size;
  off_t out_file_offset;
  int fd;
  int out_file_fd;
  fd_ctx_t ctx;
  enum body_kind out_body_kind;
  enum {
    CLIENT_READING_HEADERS,
    CLIENT_READING_BODY,
    CLIENT_REQ_COMPLETE
  } in_state;
  enum {
    CLIENT_WRITING_RESP,
    CLIENT_RESP_COMPLETE
  } out_state;
  bool closing;
};

void client_accept_conn(int epfd, int server_fd);

void client_mark_closing(client_t *client);

void client_destroy(int epfd, client_t *client);

void client_epoll_toggle_write(
  int epfd, client_t *client, bool add_write
);

void client_http_adv_state(client_t *client);

int client_handle_read(int epfd, client_t *client);

int client_handle_write(int epfd, client_t *client);

#endif

