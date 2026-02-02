#ifndef HTTP_H
#define HTTP_H

#include "client.h"

extern char *response_not_found;

void build_http_response(struct client_state *state);

#endif

