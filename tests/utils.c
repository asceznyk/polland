#include <stdio.h>
#include <assert.h>

#include "utils.h"

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

int main() {
  test_basic_match();
  test_no_match();
  test_partial_bounds();
  printf("passed all tests!\n");
  return 0;
}
