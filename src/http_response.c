#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "client.h"
#include "utils.h"
#include "http_response.h"

char *not_found =
  "HTTP/1.1 404 Not Found\r\n"
  "Content-Type: text/html\r\n"
  "Content-Length: 68\r\n"
  "Connection: close\r\n"
  "\r\n"
  "<html><body><h1>404 Not Found</h1><p>The page is missing.</p></body></html>";

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

const char *get_file_extension(char *loc) {
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

void stream_http_response(char *rpath, int client_fd, int file_fd) {
  if (file_fd == -1) {
    send(client_fd, not_found, strlen(not_found), 0);
    return;
  }
  const char *ext = get_file_extension(rpath);
  const char *mime_type = get_mime_type(ext);
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
    mime_type, content_len
  );
  send(client_fd, header, header_len, 0);
  char buf[4096];
  ssize_t n;
  while((n = read(file_fd, buf, sizeof(buf))) > 0)
    send(client_fd, buf, n, 0);
  close(file_fd);
}

void http_response(struct client_state *state) {
  if (state->buf_len <= 0) return;
  int client_fd = state->fd;
  char *method = get_http_method(state->buffer);
  if (strcmp(method, "GET") != 0) return;
  if (!strcmp(STATIC_LOCATION, "")) {
    send(client_fd, not_found, strlen(not_found), 0);
    free(method);
    return;
  }
  char *path = get_file_path_url(state->buffer, method);
  int file_fd = static_resolve_path(path);
  stream_http_response(path, client_fd, file_fd);
  free(path);
  free(method);
}


