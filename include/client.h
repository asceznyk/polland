#ifndef CLIENT_H
#define CLIENT_H

#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#include "defs.h"

struct client_state {
  int fd;
  char buffer[BUFFER_SIZE];
  ssize_t buf_len;
};

void *handle_client(void *arg);

#endif



