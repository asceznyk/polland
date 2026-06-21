#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define HOST "127.0.0.1"
#define PORT "6969"
#define RECV_TIMEOUT_SEC 5

static int connect_server(void) {
  struct addrinfo hints = {0};
  struct addrinfo *result = NULL;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(HOST, PORT, &hints, &result) != 0) {
    perror("getaddrinfo");
    return -1;
  }
  int fd = -1;
  for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0)
      continue;
    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
      break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(result);
  return fd;
}

static int send_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t n = send(fd, buf, len, 0);
    if (n <= 0)
      return -1;
    buf += n;
    len -= (size_t)n;
  }
  return 0;
}

static void recv_some(int fd) {
  char buf[8192];
  ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
  if (n < 0) {
    perror("recv");
    exit(EXIT_FAILURE);
  }
  if (n == 0) {
    printf("connection closed\n");
    return;
  }
  buf[n] = '\0';
  printf("----- recv (%zd bytes) -----\n", n);
  printf("%s\n", buf);
}

int main(void) {
  static const char req1[] =
    "GET / HTTP/1.1\r\n"
    "Host: 127.0.0.1:6969\r\n"
    "Connection: keep-alive\r\n"
    "\r\n";
  static const char req2_req3[] =
    "GET / HTTP/1.1\r\n"
    "Host: 127.0.0.1:6969\r\n"
    "Connection: close\r\n"
    "\r\n"
    "GET / HTTP/1.1\r\n"
    "Host: 127.0.0.1:6969\r\n"
    "Connection: keep-alive\r\n"
    "\r\n";
  int fd = connect_server();
  if (fd < 0) {
    fprintf(stderr, "failed to connect\n");
    return EXIT_FAILURE;
  }
  struct timeval timeout = {
    .tv_sec = RECV_TIMEOUT_SEC,
    .tv_usec = 0,
  };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  printf("=== Event #1 ===\n");
  send_all(fd, req1, sizeof(req1) - 1);
  recv_some(fd);
  printf("=== Event #2 ===\n");
  send_all(fd, req2_req3, sizeof(req2_req3) - 1);
  while (1) {
    char buf[8192];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n == 0) {
      puts("server closed connection");
      break;
    }
    if (n < 0) {
      printf("recv: %s\n", strerror(errno));
      break;
    }
    buf[n] = '\0';
    printf("----- recv (%zd bytes) -----\n", n);
    printf("%s\n", buf);
  }
  close(fd);
  return 0;
}

