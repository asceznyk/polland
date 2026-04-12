#include "config.h"

config_t server_cfg;

config_t config_parse_file(const char *fpath) {
  config_t cfg = {0};
  yyjson_read_flag flg = YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS;
  yyjson_read_err err;
  yyjson_doc *doc = yyjson_read_file(fpath, flg, NULL, &err);
  if (!doc) {
    fprintf(
      stderr,
      "read error (%u): %s at position: %ld\n",
      err.code, err.msg, err.pos
    );
    exit(1);
  }
  yyjson_val *obj = yyjson_doc_get_root(doc);
  yyjson_obj_iter iter;
  yyjson_obj_iter_init(obj, &iter);
  yyjson_val *key, *val;
  while ((key = yyjson_obj_iter_next(&iter))) {
    val = yyjson_obj_iter_get_val(key);
    const char *kname = yyjson_get_str(key);
    if (!strcmp(kname, "port")) cfg.port = yyjson_get_int(val);
    else if(!strcmp(kname, "static_prefix"))
      cfg.static_prefix = strdup(yyjson_get_str(val));
    else if(!strcmp(kname, "static_root"))
      cfg.static_root = strdup(yyjson_get_str(val));
    else if (!strcmp(kname, "upstream")) {
      yyjson_val *upstream = yyjson_obj_get(obj, "upstream");
      if (!upstream || !yyjson_is_obj(upstream)) {
        fprintf(stderr, "`upstream` must be an object\n");
        exit(1);
      }
      yyjson_val *host_val = yyjson_obj_get(upstream, "host");
      if (!host_val || !yyjson_is_str(host_val)) {
        fprintf(stderr, "`upstream.host` must be a string\n");
        exit(1);
      }
      yyjson_val *port_val = yyjson_obj_get(upstream, "port");
      if (!port_val || !yyjson_is_int(port_val)) {
        fprintf(stderr, "`upstream.port` must be an integer\n");
        exit(1);
      }
      const char *host = yyjson_get_str(host_val);
      int port = yyjson_get_int(port_val);
      if (port < 1 || port > 65535) {
        fprintf(stderr, "`upstream.port` out of range\n");
        exit(1);
      }
      cfg.upstream.host = strdup(host);
      cfg.upstream.port = (uint16_t)port;
    }
  }
  yyjson_doc_free(doc);
  return cfg;
}

