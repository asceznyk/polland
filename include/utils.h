#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>
#include <string.h>
#include <stddef.h>
#include <sys/types.h>

#include "client.h"

char *skip_leading_ws(char *buffer);

int ends_with_double_crlf(const char *buf, size_t len);

ssize_t find_double_crlf(char *buf, size_t len, size_t start);

void print_escaped(const char *buf, ssize_t n);

char *str_concat(char* a, char* b);

void print_client_io_buffers(struct client_state *client);

#endif

