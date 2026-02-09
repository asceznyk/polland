#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "defs.h"
#include "client.h"
#include "utils.h"
#include "http.h"

char *not_found_header =
  "HTTP/1.1 404 Not Found\r\n"
  "Content-Type: text/html\r\n"
  "Content-Length: 68\r\n"
  "Connection: close\r\n"
  "\r\n";

char *not_found_body =
  "<html><body><h1>404 Not Found</h1><p>The page is missing.</p></body></html>";

char *not_implemented_header =
  "HTTP/1.1 501 Not Implemented\r\n"
  "Content-Type: text/html\r\n"
  "Content-Length: 90\r\n"
  "Connection: keep-alive\r\n"
  "\r\n";

char *not_implemented_body =
  "<html><body><h1>501 Not Implemented</h1><p>This method is not supported.</p></body></html>";

enum http_method http_parse_method(char *data, size_t hdr_end) {
  if (hdr_end >= 3 && !memcmp(data, "GET", 3)) return M_GET;
  if (hdr_end >= 4 && !memcmp(data, "HEAD", 4)) return M_HEAD;
  if (hdr_end >= 4 && !memcmp(data, "POST", 4)) return M_POST;
  if (hdr_end >= 3 && !memcmp(data, "PUT", 3)) return M_PUT;
  if (hdr_end >= 6 && !memcmp(data, "DELETE", 6)) return M_DELETE;
  return M_UNKNOWN;
}

char *http_parse_url(char *data) {
  char *method = strtok(data, " \t");
  char *url = strtok(NULL, " \t");
  char *ver = strtok(NULL, " \t");
  if (!method || !url || !ver) return NULL;
  return url;
}

char *http_get_file_extension(char *loc) {
  char *dot = strrchr(loc, '.');
  if(!dot || dot == loc) return "";
  return dot + 1;
}

const char *http_get_mime_type(const char *ext) {
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

char *http_resolve_static_path(const char *url) {
  const char *prefix = STATIC_PREFIX;
  const char *root = STATIC_ROOT;
  const char *index = "/index.html";
  size_t ulen = strlen(url);
  size_t plen = strlen(prefix);
  if (strncmp(url, prefix, plen) != 0) return NULL;
  if (ulen == plen || (ulen == plen + 1 && url[plen] == '/')) {
    size_t len = strlen(root) + strlen(index) + 1;
    char *out = malloc(len);
    if (!out) return NULL;
    strcpy(out, root);
    strcat(out, index);
    return out;
  }
  if (url[plen] == '/') {
    const char *rest = url + plen;
    size_t len = strlen(root) + strlen(rest) + 1;
    char *out = malloc(len);
    if (!out) return NULL;
    strcpy(out, root);
    strcat(out, rest);
    return out;
  }
  return NULL;
}

int http_open_static_path(char *url) {
  char *loc = http_resolve_static_path(url);
  struct stat st;
  if (stat(loc, &st) == 0 && S_ISDIR(st.st_mode)) {
    free(loc);
    return -1;
  }
  int fd = open(loc, O_RDONLY);
  free(loc);
  return fd;
}

void http_build_static_response(
  struct client_state *client, char *url, int file_fd, bool is_head
) {
  const char *mime_type = NULL;
  printf("url = %s\n", url);
  if(strcmp(url, STATIC_PREFIX) == 0 || strcmp(url, STATIC_PREFIX "/") == 0) {
    mime_type = http_get_mime_type("html");
  } else {
    char *ext = http_get_file_extension(url);
    mime_type = http_get_mime_type(ext);
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
  struct buffer *out_headers = &client->out_headers;
  out_headers->data = malloc((size_t)header_len);
  memcpy(out_headers->data, header, (size_t)header_len);
  out_headers->len = header_len;
  out_headers->cap = header_len;
  if (is_head) {
    close(file_fd);
    return;
  }
  client->out_body_kind = BODY_FILE;
  client->out_file_fd = file_fd;
  client->out_file_size = (size_t)content_len;
}

void http_fill_buffer(struct buffer *buf, const char *fill_buf) {
  size_t n = strlen(fill_buf);
  char *data = malloc(n);
  if (!data) return;
  free(buf->data);
  buf->data = data;
  buf->cap = n;
  buf->len = n;
  memcpy(buf->data, fill_buf, n);
}

void http_build_err_response(
  struct client_state *client,
  const char *err_header,
  const char *err_body,
  bool is_head
) {
  http_fill_buffer(&client->out_headers, err_header);
  if (is_head) return;
  http_fill_buffer(&client->out_body, err_body);
}

void http_fill_response_get(struct client_state *client, bool is_head)  {
  struct buffer *in_headers = &client->in_headers;
  char *url = http_parse_url(in_headers->data);
  if (strncmp(url, STATIC_PREFIX, strlen(STATIC_PREFIX)) == 0) {
    int file_fd = http_open_static_path(url);
    printf("http_fill_response_get, file_fd = %d\n", file_fd);
    if (file_fd == -1) {
      http_build_err_response(
        client, not_found_header, not_found_body, is_head
      );
      return;
    }
    http_build_static_response(client, url, file_fd, is_head);
    return;
  } /*else {
    //deal with proxy request(s)
  }*/
  http_build_err_response(
    client, not_implemented_header, not_implemented_body, is_head
  );
  return;
}

void http_build_out_response(struct client_state *client, size_t hdr_end) {
  struct buffer *in_headers = &client->in_headers;
  enum http_method method = http_parse_method(in_headers->data, hdr_end);
  if (method == M_HEAD || method == M_GET) {
    http_fill_response_get(client, (method == M_HEAD));
  }
}



