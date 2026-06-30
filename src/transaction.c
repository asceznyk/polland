#include "transaction.h"

bool transaction_init(transaction_t *transaction) {
  transaction->req_is_static = false;
  transaction->req_is_http_one_point_o = false;
  transaction->req_is_connection_close = false;
  transaction->resp_header_complete = false;
  transaction->req_len = 0;
  transaction->resp_len = 0;
  transaction->resp_header_content_len = 0;
  return true;
}

