#include <stdio.h>
#include <assert.h>

#include "buffer.h"
#include "utils.h"
#include "http.h"

static void test_basic_match(void) {
  char *buf = "GET / HTTP/1.1\r\nHost: x\r\n\r\nBODY";
  ssize_t p = find_double_crlf(buf, strlen(buf), 0);
  assert(p == 27);
}

static void test_no_match() {
  char *buf = "GET / HTTP/1.1\r\nHost: x\r\n";
  ssize_t p = find_double_crlf(buf, strlen(buf), 0);
  assert(p == -1);
}

static void test_partial_bounds() {
  char *buf = "\r\n\r";
  ssize_t p = find_double_crlf(buf, strlen(buf), 0);
  assert(p == -1);
}

static void test_parse_method_head() {
  char *buf = "HEAD / HTTP/1.1\r\nHost: x\r\n\r\nBODY";
  ssize_t p = find_double_crlf(buf, strlen(buf), 0);
  enum http_method method = http_parse_method(buf, p);
  assert(method == M_HEAD);
}

static void test_parse_url_root() {
  char buf[] = "GET / HTTP/1.1\r\n\r\n";
  char *url = http_parse_url(buf);
  assert(url);
  assert(strcmp(url, "/") == 0);
}

static void test_parse_url_extra_spaces() {
  char buf[] = "GET    /foo/bar    HTTP/1.1\r\n\r\n";
  char *url = http_parse_url(buf);
  assert(url);
  assert(strcmp(url, "/foo/bar") == 0);
}

static void test_buffer_init() {
  buffer_t buf = {0};
  bool ok = buffer_init(&buf, 128);
  assert(ok);
  assert(buf.data);
  assert(buf.len == 0);
  assert(buf.cap == 128);
  buffer_free(&buf);
  assert(buf.data == NULL);
  assert(buf.len == 0);
  assert(buf.cap == 0);
}

int main() {
  test_basic_match();
  test_no_match();
  test_partial_bounds();
  test_parse_method_head();
  test_parse_url_root();
  test_parse_url_extra_spaces();
  test_buffer_init();
  printf("passed all tests!\n");
  return 0;
}
