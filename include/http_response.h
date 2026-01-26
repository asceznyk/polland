#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "client.h"

extern char *response_not_found;

void http_response(struct client_state *state);

#endif

