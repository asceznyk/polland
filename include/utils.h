#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>
#include <string.h>
#include <stddef.h>

char *skip_leading_ws(char *buffer);

int ends_with_double_crlf(const char *buf, size_t len);

void print_escaped(const char *buf, ssize_t n);

char *str_concat(char* a, char* b);

#endif

