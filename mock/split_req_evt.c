#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT "4545"
#define RECV_TIMEOUT_SEC 5

static void usage(const char *prog) {
  fprintf(stderr, "Usage: %s [--url HOST:PORT | --host HOST --port PORT]\n", prog);
  fprintf(stderr, "  --url HOST:PORT   specify host and port together\n");
  fprintf(stderr, "  --host HOST       specify host (default: %s)\n", DEFAULT_HOST);
  fprintf(stderr, "  --port PORT       specify port (default: %s)\n", DEFAULT_PORT);
}

static int parse_url(const char *url, char *host, size_t host_len,
                     char *port, size_t port_len) {
  const char *colon = strrchr(url, ':');
  if (colon == NULL) {
    fprintf(stderr, "invalid --url format, expected HOST:PORT\n");
    return -1;
  }
  size_t hlen = (size_t)(colon - url);
  if (hlen == 0 || hlen >= host_len) {
    fprintf(stderr, "host part of --url is empty or too long\n");
    return -1;
  }
  memcpy(host, url, hlen);
  host[hlen] = '\0';
  size_t plen = strlen(colon + 1);
  if (plen == 0 || plen >= port_len) {
    fprintf(stderr, "port part of --url is empty or too long\n");
    return -1;
  }
  memcpy(port, colon + 1, plen);
  port[plen] = '\0';
  return 0;
}

static int connect_server(const char *host, const char *port) {
  struct addrinfo hints = {0};
  struct addrinfo *result = NULL;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, port, &hints, &result) != 0) {
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

int main(int argc, char *argv[]) {
  char host[256] = DEFAULT_HOST;
  char port[16]  = DEFAULT_PORT;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--url") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--url requires an argument\n");
        usage(argv[0]);
        return EXIT_FAILURE;
      }
      if (parse_url(argv[++i], host, sizeof(host), port, sizeof(port)) != 0)
        return EXIT_FAILURE;
    } else if (strcmp(argv[i], "--host") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--host requires an argument\n");
        usage(argv[0]);
        return EXIT_FAILURE;
      }
      snprintf(host, sizeof(host), "%s", argv[++i]);
    } else if (strcmp(argv[i], "--port") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--port requires an argument\n");
        usage(argv[0]);
        return EXIT_FAILURE;
      }
      snprintf(port, sizeof(port), "%s", argv[++i]);
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      usage(argv[0]);
      return EXIT_FAILURE;
    }
  }
  char host_header[272];
  snprintf(host_header, sizeof(host_header), "%s:%s", host, port);
  char req1[512];
  snprintf(req1, sizeof(req1),
    "GET / HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Connection: keep-alive\r\n"
    "\r\n",
    host_header);
  char req2_req3[1024];
  snprintf(req2_req3, sizeof(req2_req3),
    "GET / HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Connection: keep-alive\r\n"
    "\r\n"
    "GET / HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Connection: close\r\n"
    "\r\n",
    host_header, host_header);
  int fd = connect_server(host, port);
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
  send_all(fd, req1, strlen(req1));
  recv_some(fd);
  printf("=== Event #2 ===\n");
  send_all(fd, req2_req3, strlen(req2_req3));
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

