#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>

#include "client.h"

static atomic_bool running = 1;

void handle_interrupt(int sig) {
  printf("SIGINT/SIGTERM recieved, stopping the server..\n");
  running = 0;
}

int main() {
  signal(SIGINT, handle_interrupt);
  signal(SIGTERM, handle_interrupt);
  int server_fd;
  int opt = 1;
  if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("failed to create socket!\n");
    exit(EXIT_FAILURE);
  }
  setsockopt(
    server_fd,
    SOL_SOCKET,
    SO_REUSEADDR,
    &opt,
    sizeof(opt)
  );
  struct sockaddr_in server_addr = {0};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);
  if (
    bind(
      server_fd,
      (struct sockaddr*)&server_addr,
      sizeof(server_addr)
    ) < 0
  ) {
    perror("failed bind to socket address\n");
    exit(EXIT_FAILURE);
  }
  if(listen(server_fd, NUM_BACKLOG) < 0) {
    perror("failed to listen!\n");
    exit(EXIT_FAILURE);
  }
  printf("server is listening on PORT %d\n", PORT);
  printf("static dir = %s\n", STATIC_LOCATION);
  int epfd = epoll_create1(0);
  struct epoll_event evt = {
    .events = EPOLLIN,
    .data.fd = server_fd
  };
  epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &evt);
  struct epoll_event sevents[MAX_EVENTS];
  int flags = fcntl(server_fd, F_GETFL, 0);
  fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
  while (running) {
    int n = epoll_wait(epfd, sevents, MAX_EVENTS, -1);
    for (int i = 0; i < n; i++) {
      uint32_t events = sevents[i].events;
      if (sevents[i].data.fd == server_fd) {
        add_client_conn(epfd, server_fd);
        continue;
      }
      struct client_state *client = sevents[i].data.ptr;
      if (events & (EPOLLERR | EPOLLHUP)) {
        close_and_free_client(epfd, client);
        continue;
      }
      if (events & (EPOLLIN | EPOLLRDHUP)) {
        handle_client_read(epfd, client);
      }
      if (events & EPOLLOUT) {
        handle_client_write(epfd, client);
      }
    }
  }
  printf("closing all epolls and main server thread...\n");
  epoll_ctl(epfd, EPOLL_CTL_DEL, server_fd, NULL);
  close(server_fd);
  close(epfd);
  return 0;
}

