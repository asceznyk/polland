#ifndef CLIENT_H
#define CLIENT_H

#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#include "defs.h"

struct client_state {
  int fd;
  size_t in_len;
  size_t in_pos;
  char in_buf[BUFFER_SIZE];
  size_t out_len;
  size_t out_sent;
  char *out_buf;
  enum {
    STATE_READING,
    STATE_WRITING,
    STATE_CLOSING
  } state;
};

void add_client_conn(int epfd, int server_fd);

void close_and_free_client(int epfd, struct client_state *client);

void handle_client_read(int epfd, struct client_state *client);

void handle_client_write(int epfd, struct client_state *client);

#endif



