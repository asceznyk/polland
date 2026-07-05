#include <stdio.h>
#include <stdlib.h>

#include "log.h"
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
  char out[4*BUFFER_SIZE];
  size_t pos = 0;
  if (n) pos += snprintf(out + pos, sizeof(out) - pos, "print_escaped: ");
  for (ssize_t i = 0; i < n && pos < sizeof(out) - 5; i++) {
    unsigned char c = buf[i];
    if (c == '\r') pos += snprintf(out + pos, sizeof(out) - pos, "\\r");
    else if (c == '\n') pos += snprintf(out + pos, sizeof(out) - pos, "\\n");
    else if (c == '\t') pos += snprintf(out + pos, sizeof(out) - pos, "\\t");
    else if (c < 32 || c > 126) pos += snprintf(out + pos, sizeof(out) - pos, "\\x%02X", c);
    else out[pos++] = (char)c;
  }
  out[pos] = '\0';
  LOG_DEBUG("%s", out);
}

void print_client_in_stream(client_t *client) {
  buffer_t *in_stream = &client->in_stream;
  LOG_DEBUG("in_stream = "); print_escaped(in_stream->data, in_stream->len);
}

void print_client_out_stream(client_t *client) {
  buffer_t *out_stream = &client->out_stream;
  LOG_DEBUG("out_stream = "); print_escaped(out_stream->data, out_stream->len);
}

