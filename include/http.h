#ifndef HTTP_H
#define HTTP_H

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "client.h"

extern const char *NOT_FOUND_HEADER;
extern const char *NOT_FOUND_BODY;
extern const char *METHOD_NOT_ALLOWED_HEADER;
extern const char *METHOD_NOT_ALLOWED_BODY;
extern const char *NOT_IMPLEMENTED_HEADER;
extern const char *NOT_IMPLEMENTED_BODY;
extern const char *BAD_GATEWAY_HEADER;
extern const char *BAD_GATEWAY_BODY;

enum http_method { M_GET, M_HEAD, M_POST, M_PUT, M_DELETE, M_UNKNOWN };

bool http_is_one_point_o(char *data, size_t len);

enum http_method http_parse_method(char *buf, size_t hdr_end);

char *http_parse_url(char *buf);

void http_build_err_resp(
  client_t *client,
  const char *err_header,
  const char *err_body,
  bool is_head
);

void http_build_out_resp(client_t *client, size_t hdr_end);

bool http_is_resp_complete(struct buffer *buf);

#endif
