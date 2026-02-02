#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 6969
#define BUF_SIZE    4096

int main(void) {
  int sockfd;
  struct sockaddr_in server_addr;
  char buf[BUF_SIZE];
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    return 1;
  }
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port   = htons(SERVER_PORT);
  if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
    perror("inet_pton");
    close(sockfd);
    return 1;
  }
  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    close(sockfd);
    return 1;
  }
  const char *req =
    "GET / HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "Connection: close\r\n"
    "\r\n";
  if (write(sockfd, req, strlen(req)) < 0) {
    perror("write");
    close(sockfd);
    return 1;
  }
  /*
  ssize_t n;
  while ((n = read(sockfd, buf, sizeof(buf) - 1)) > 0) {
    buf[n] = '\0';
    fputs(buf, stdout);
  }
  if (n < 0) {
    perror("read");
  }*/
  close(sockfd);
  return 0;
}

