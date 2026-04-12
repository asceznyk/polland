#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <yyjson.h>

typedef struct {
  uint16_t port;
  const char *static_root;
  const char *static_prefix;
  struct {
    const char *host;
    uint16_t port;
  } upstream;
} config_t;

extern config_t server_cfg;

config_t config_parse_file(const char *fpath);

#endif


