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
  if (n) printf("print_escaped:\t");
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

char *str_concat(char *a, char *b) {
  size_t len_a = strlen(a);
  size_t len_b = strlen(b);
  char *res = malloc(len_a + len_b + 1);
  memcpy(res, a, len_a);
  memcpy(res + len_a, b, len_b);
  res[len_a + len_b] = '\0';
  return res;
}

void print_client_in_buffers(struct client_state *client) {
  struct buffer *in_headers = &client->in_headers;
  struct buffer *in_body = &client->in_body;
  printf("in_headers = "); print_escaped(in_headers->data, in_headers->len);
  printf("in_body = "); print_escaped(in_body->data, in_body->len);
}

void print_client_out_buffers(struct client_state *client) {
  struct buffer *out_headers = &client->out_headers;
  struct buffer *out_body = &client->out_body;
  printf("out_headers = "); print_escaped(out_headers->data, out_headers->len);
  printf("out_body = "); print_escaped(out_body->data, out_body->len);
}


