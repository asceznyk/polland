#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "client.h"

char *skip_leading_ws(char *buffer) {
  while(*buffer && isspace((unsigned char)*buffer)) buffer++;
  return buffer;
}

ssize_t find_double_crlf(char *buf, size_t len, size_t start) {
  for (size_t i = start; i + 3 < len; i++) {
    if (
      buf[i] == '\r' && buf[i+1] == '\n' &&
      buf[i+2] == '\r' && buf[i+3] == '\n'
    ) return i + 4;
  }
  return -1;
}

int ends_with_double_crlf(const char *buf, size_t len) {
  if (len < 4) return 0;
  return buf[len-4] == '\r' &&
  buf[len-3] == '\n' &&
  buf[len-2] == '\r' &&
  buf[len-1] == '\n';
}

void print_escaped(const char *buf, ssize_t n) {
  if (n) printf("print_escaped: ");
  for (ssize_t i = 0; i < n; i++) {
    unsigned char c = buf[i];
    if (c == '\r') printf("\\r");
    else if (c == '\n') printf("\\n");
    else if (c == '\t') printf("\\t");
    else if (c < 32 || c > 126) printf("\\x%02X", c);
    else printf("%c", c);
  }
  printf("\n");
}

void print_client_in_stream(client_t *client) {
  struct buffer *in_stream = &client->in_stream;
  printf("in_stream = "); print_escaped(in_stream->data, in_stream->len);
}

void print_client_out_stream(client_t *client) {
  struct buffer *out_stream = &client->out_stream;
  printf("out_stream = "); print_escaped(out_stream->data, out_stream->len);
}

