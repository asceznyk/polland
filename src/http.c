#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "defs.h"
#include "client.h"
#include "utils.h"
#include "http.h"

char *not_found =
  "HTTP/1.1 404 Not Found\r\n"
  "Content-Type: text/html\r\n"
  "Content-Length: 68\r\n"
  "Connection: close\r\n"
  "\r\n"
  "<html><body><h1>404 Not Found</h1><p>The page is missing.</p></body></html>";

void fill_client_not_found(struct client_state *client) {
  size_t n = strlen(not_found);
  client->out_buf = malloc(n);
  client->out_len = n;
  memcpy(client->out_buf, not_found, n);
}

char *get_http_method(char *buffer) {
  char *end = skip_leading_ws(buffer);
  while (*end && *end != ' ') end++;
  size_t len = end - buffer;
  char *method = malloc(len + 1);
  if(!method) return NULL;
  memcpy(method, buffer, len);
  method[len] = '\0';
  return method;
}

char *get_file_path_url(char *buffer, char *method) {
  char *start = skip_leading_ws(buffer);
  start += strlen(method)+1;
  char *end = start;
  while(*end && *end != ' ') end++;
  size_t len = end - start;
  char *loc = malloc(len + 1);
  memcpy(loc, start, len);
  loc[len] = '\0';
  return loc;
}

char *get_file_extension(char *loc) {
  char *dot = strrchr(loc, '.');
  if(!dot || dot == loc) return "";
  return dot + 1;
}

const char *get_mime_type(const char *ext) {
  if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm")) {
    return "text/html";
  } else if (!strcasecmp(ext, "jpeg") || !strcasecmp(ext, "jpg")) {
    return "image/jpeg";
  } else if (!strcasecmp(ext, "txt")) {
    return "text/plain";
  } else if (!strcasecmp(ext, "png")) {
    return "image/png";
  } else {
    return "application/octet-stream";
  }
}

int static_resolve_path(char *path) {
  char *loc = str_concat(STATIC_LOCATION, path);
  struct stat st;
  if (stat(loc, &st) == 0 && S_ISDIR(st.st_mode)) {
    free(loc);
    return static_resolve_path("/index.html");
  }
  int fd = open(loc, O_RDONLY);
  free(loc);
  return fd;
}

void put_http_response(
  struct client_state *client, char *rpath, int file_fd
) {
  if (file_fd == -1) {
    fill_client_not_found(client);
    return;
  }
  const char *mime_type = NULL;
  if(strcmp(rpath, "/") == 0) {
    mime_type = get_mime_type("html");
  } else {
    char *ext = get_file_extension(rpath);
    mime_type = get_mime_type(ext);
  }
  struct stat f_stat;
  fstat(file_fd, &f_stat);
  off_t content_len = f_stat.st_size;
  char header[256];
  int header_len = snprintf(
    header, sizeof(header),
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %zu\r\n"
      "Connection: keep-alive\r\n"
      "\r\n",
    mime_type, (size_t)content_len
  );
  ssize_t n;
  char tmp_buf[BUFFER_SIZE];
  client->out_buf = malloc((size_t)header_len+content_len);
  client->out_len = 0;
  client->out_sent = 0;
  memcpy(client->out_buf, header, (size_t)header_len);
  client->out_len += (size_t)header_len;
  while((n = read(file_fd, tmp_buf, sizeof(tmp_buf))) > 0) {
    memcpy(client->out_buf+client->out_len, tmp_buf, n);
    client->out_len += n;
  };
  close(file_fd);
}

void build_http_response(struct client_state *client) {
  if (client->in_len <= 0) return;
  int client_fd = client->fd;
  char *method = get_http_method(client->in_buf);
  if (strcmp(method, "GET") != 0) return;
  if (!strcmp(STATIC_LOCATION, "")) {
    fill_client_not_found(client);
    free(method);
    return;
  }
  char *path = get_file_path_url(client->in_buf, method);
  int file_fd = static_resolve_path(path);
  put_http_response(client, path, file_fd);
  free(path);
  free(method);
}


