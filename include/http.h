#ifndef HTTP_H
#define HTTP_H

#include "client.h"

enum http_method { M_GET, M_HEAD, M_POST, M_PUT, M_DELETE, M_UNKNOWN };

enum http_method http_parse_method(char *buf, size_t hdr_end);

char *http_parse_url(char *buf);

void http_build_out_resp(struct client_state *client, size_t hdr_end);

#endif

