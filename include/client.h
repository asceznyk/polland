#ifndef CLIENT_H
#define CLIENT_H

#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#include "defs.h"
#include "buffer.h"

enum body_kind {
  BODY_NONE,
  BODY_BUFFER,
  BODY_FILE
};

struct client_state {
  int fd;
  bool in_has_body;
  struct buffer in_headers;
  struct buffer in_body;
  struct buffer out_headers;
  size_t out_headers_sent;
  struct buffer out_body;
  enum body_kind out_body_kind;
  size_t out_body_sent;
  int out_file_fd;
  size_t out_file_size;
  off_t out_file_offset;
  enum {
    STATE_READING_HEADERS,
    STATE_READING_BODY,
    STATE_WRITING_HEADERS,
    STATE_WRITING_BODY,
    STATE_CLOSING,
    STATE_IDLE
  } state;
};

void client_accept_conn(int epfd, int server_fd);

void client_close_and_free(int epfd, struct client_state *client);

void client_handle_read(int epfd, struct client_state *client);

void client_handle_write(int epfd, struct client_state *client);

#endif



