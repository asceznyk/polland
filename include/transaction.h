#ifndef TRANSACTION_H
#define TRANSACTION_H

 typedef struct {
  size_t req_len;
  size_t resp_len;
  size_t resp_body_len;
  size_t resp_header_content_len;
  ssize_t resp_header_len;
  bool req_is_static;
  bool req_is_http_one_point_o;
  bool req_is_connection_close;
  bool resp_header_complete;
} transaction_t;

bool transaction_init(transaction_t *transaction);

#endif

