#include "config.h"
#include "utils.h"
#include "http.h"
#include "buffer.h"

const char *NOT_FOUND_HEADER =
  "HTTP/1.1 404 Not Found\r\n"
  "Content-Type: text/html\r\n"
  "Content-Length: 68\r\n"
  "Connection: close\r\n"
  "\r\n";

const char *NOT_FOUND_BODY =
  "<html><body><h1>404 Not Found</h1><p>The page is missing.</p></body></html>";

const char *METHOD_NOT_ALLOWED_HEADER =
  "HTTP/1.1 405 Method Not Allowed\r\n"
  "Content-Type: text/html\r\n"
  "Content-Length: 123\r\n"
  "Allow: GET, HEAD\r\n"
  "Connection: close\r\n"
  "\r\n";

const char *METHOD_NOT_ALLOWED_BODY =
  "<html><body><h1>405 Method Not Allowed</h1>"
  "<p>The requested HTTP method is not supported for this resource.</p>"
  "</body></html>";

const char *NOT_IMPLEMENTED_HEADER =
  "HTTP/1.1 501 Not Implemented\r\n"
  "Content-Type: text/html\r\n"
  "Content-Length: 90\r\n"
  "Connection: keep-alive\r\n"
  "\r\n";

const char *NOT_IMPLEMENTED_BODY =
  "<html><body><h1>501 Not Implemented</h1><p>This method is not supported.</p></body></html>";

const char *BAD_GATEWAY_HEADER =
  "HTTP/1.1 502 Bad Gateway\r\n"
  "Content-Type: text/html\r\n"
  "Content-Length: 106\r\n"
  "\r\n";

const char *BAD_GATEWAY_BODY =
  "<html><body><h1>502 Bad Gateway</h1><p>The upstream server returned an invalid response.</p></body></html>";

bool http_is_one_point_o(char *data, size_t len) {
  char *line_end = memmem(data, len, "\r\n", 2);
  if (!line_end) return false;
  char *http = memmem(data, line_end - data, "HTTP/", 5);
  if (!http) return false;
  if ((line_end - http) < 8) return false;
  return (memcmp(http, "HTTP/1.0", 8) == 0);
}

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
  const char *prefix = server_cfg.static_prefix;
  const char *root = server_cfg.static_root;
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
  if (!loc) return -1;
  int fd = open(loc, O_RDONLY);
  free(loc);
  if (fd < 0) return -1;
  if (fd <= 2) {
    int newfd = fcntl(fd, F_DUPFD, 3);
    close(fd);
    fd = newfd;
    if (fd < 0) return -1;
  }
  struct stat st;
  if (fstat(fd, &st) < 0 || S_ISDIR(st.st_mode)) {
    close(fd);
    return -1;
  }
  return fd;
}

void http_fill_static_file_content(
  client_t *client, char *url, int file_fd, bool is_head
) {
  const char *mime_type = NULL;
  printf("http_fill_static_file_content: url = %s\n", url);
  size_t len = strlen(server_cfg.static_prefix);
  printf("http_fill_static_file_content: server_cfg.static_prefix = %s\n", server_cfg.static_prefix);
  if (
    strcmp(url, server_cfg.static_prefix) == 0 ||
    (strncmp(url, server_cfg.static_prefix, len) == 0 && url[len] == '/' && url[len+1] == '\0')
  ) {
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
  buffer_t *out_stream = &client->out_stream;
  memcpy(out_stream->data, header, (size_t)header_len);
  out_stream->len = header_len;
  if (is_head) {
    close(file_fd);
    return;
  }
  client->out_body_kind = BODY_FILE;
  client->out_file_fd = file_fd;
  client->out_file_size = (size_t)content_len;
}

void http_fill_buffer(buffer_t *buf, const char *fill_buf) {
  size_t n = strlen(fill_buf);
  buf->len = n;
  memcpy(buf->data, fill_buf, n);
}

void http_build_err_resp(
  client_t *client,
  const char *err_header,
  const char *err_body,
  bool is_head
) {
  http_fill_buffer(&client->out_stream, err_header);
  if (is_head) return;
  buffer_append(&client->out_stream, err_body, strlen(err_body));
}

void http_fill_static_resp_get(
  client_t *client, char *url, bool is_head
) {
  client->out_body_kind = is_head ? BODY_NONE : BODY_BUFFER;
  int file_fd = http_open_static_path(url);
  if (file_fd == -1) {
    http_build_err_resp(
      client, NOT_FOUND_HEADER, NOT_FOUND_BODY, is_head
    );
    return;
  }
  printf("http_fill_static_resp_get: file_fd = %d\n", file_fd);
  http_fill_static_file_content(client, url, file_fd, is_head);
}

void http_build_static_resp(client_t *client, size_t hdr_end) {
  buffer_t *in_stream = &client->in_stream;
  char tmp[BUFFER_SIZE];
  memcpy(tmp, in_stream->data, hdr_end);
  tmp[hdr_end] = '\0';
  char *url = http_parse_url(tmp);
  enum http_method method = http_parse_method(in_stream->data, hdr_end);
  if (method == M_HEAD || method == M_GET) {
    http_fill_static_resp_get(client, url, (method == M_HEAD));
    return;
  }
  http_build_err_resp(
    client, METHOD_NOT_ALLOWED_HEADER, METHOD_NOT_ALLOWED_BODY, false
  );
}

bool http_is_static_url(client_t *client, size_t hdr_end) {
  buffer_t *in_stream = &client->in_stream;
  char tmp[BUFFER_SIZE];
  memcpy(tmp, in_stream->data, hdr_end);
  tmp[hdr_end] = '\0';
  char *url = http_parse_url(tmp);
  if (
    strncmp(
      url,
      server_cfg.static_prefix,
      strlen(server_cfg.static_prefix)
    ) == 0
  ) {
    return true;
  }
  return false;
}

ssize_t http_get_content_length(buffer_t *buf) {
  const char *headers_end = buf->data;
  size_t pos = (size_t)find_double_crlf(buf->data, buf->len, 0);
  headers_end = headers_end + pos;
  const char *p = buf->data;
  while (p < headers_end) {
    const char *line_end = p;
    while (
      line_end < headers_end &&
      !(line_end[0] == '\r' && line_end[1] == '\n')
    ) {
      line_end++;
    }
    size_t line_len = line_end - p;
    if (
      line_len >= 15 &&
      strncasecmp(p, "Content-Length:", 15) == 0
    ) {
      const char *v = p + 15;
      while (v < line_end && (*v == ' ' || *v == '\t'))
        v++;
      ssize_t content_length = 0;
      while (v < line_end && isdigit((unsigned char)*v)) {
        content_length = content_length * 10 + (*v - '0');
        v++;
      }
      return content_length;
    }
    p = line_end + 2;
  }
  return -1;
}

bool http_req_is_body_complete(buffer_t *buf) {
  char *data = buf->data;
  size_t len = buf->len;
  enum http_method method = http_parse_method(data, len);
  if (method == M_GET || method == M_HEAD)
    return (find_double_crlf(data, len, 0) >= 0);
  return false; //FOR NOW!
}

bool http_resp_is_body_complete(buffer_t *buf) {
  char *hdr = memmem(buf->data, buf->len, "\r\n\r\n", 4);
  if (!hdr)
    return false;
  ssize_t content_length = http_get_content_length(buf);
  if (content_length < 0)
    return false;
  size_t body_length = buf->len - (hdr + 4 - buf->data);
  return body_length >= (size_t)content_length;
}

char *http_get_connection_value(buffer_t *buf, size_t req_len) {
  char *end = buf->data + req_len;
  char *p = buf->data;
  while (p < end) {
    char *line_end = p;
    while (line_end + 1 < end && !(line_end[0] == '\r' && line_end[1] == '\n'))
      line_end++;
    if (line_end - p >= 11 && strncasecmp(p, "Connection:", 11) == 0) {
      char *v = p + 11;
      while (v < line_end && (*v == ' ' || *v == '\t')) v++;
      char *v_end = line_end;
      while (
        v_end > v &&
        (v_end[-1] == '\r' || v_end[-1] == '\n' || v_end[-1] == ' ' || v_end[-1] == '\t')
      ) v_end--;
      return strndup(v, v_end-v);
    }
    if (line_end + 2 > end) break;
    p = line_end + 2;
  }
  return NULL;
}

bool http_is_connection_close(buffer_t *buf, size_t req_len) {
  printf("http_is_connection_close: buf = %p, req_len = %ld\n", buf, req_len);
  char *val = http_get_connection_value(buf, req_len);
  printf("http_is_connection_close: val = %s\n", val);
  if (!val) return false;
  bool is_close = (strncmp(val, "close", 5) == 0);
  free(val);
  return is_close;
}



