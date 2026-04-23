#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>

#include "config.h"
#include "client.h"
#include "backend.h"

static atomic_bool running = 1;

void handle_interrupt(int sig) {
  printf("SIGINT/SIGTERM recieved, stopping the server..\n");
  running = 0;
}

int main() {
  server_cfg = config_parse_file("config/rgnx.json");
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
  server_addr.sin_port = htons(server_cfg.port);
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
  printf("server is listening on port %d\n", server_cfg.port);
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
    struct client_state *free_head = NULL;
    for (int i = 0; i < n; i++) {
      uint32_t events = sevents[i].events;
      if (sevents[i].data.fd == server_fd) {
        client_accept_conn(epfd, server_fd);
        continue;
      }
      struct fd_ctx *ctx = sevents[i].data.ptr;
      printf("ctx->kind = %d, ctx->closing = %d, ctx->client.fd = %d\n", ctx->kind, ctx->closing, ctx->client->fd);
      if (ctx->closing) continue;
      //printf("ctx->kind = %d!\n", ctx->kind);
      struct client_state *client = ctx->client;
      if (client->closing) {
        printf("client %d marked as closing!\n", client->fd);
        continue;
      }
      int dead = 0;
      if (events & (EPOLLERR | EPOLLHUP)) {
        printf("EPOLLERR | EPOLLHUP\n");
        dead = 1;
        if (ctx->kind == FD_BACKEND) backend_handle_err(epfd, client);
        else client_mark_closing(client);
        continue;
      }
      if (ctx->kind == FD_CLIENT) {
        if (events & (EPOLLIN | EPOLLRDHUP)) {
          if (client_handle_read(epfd, client) < 0) dead = 1;
        }
        if (events & EPOLLOUT) {
          if (client_handle_write(epfd, client) < 0) dead = 1;
        }
      } else if (ctx->kind == FD_BACKEND) {
        if (events & (EPOLLIN | EPOLLHUP)) {
          if (backend_handle_read(epfd, client) < 0) dead = 1;
        }
        if (events & EPOLLOUT) {
          if (backend_handle_write(epfd, client) < 0) dead = 1;
        }
      }
      if (dead) {
        client->next_to_free = free_head;
        free_head = client;
      }
    }
    while (free_head) {
      struct client_state *next = free_head->next_to_free;
      client_destroy(epfd, free_head);
      free_head = next;
    }
  }
  printf("closing all clients and main server thread...\n");
  epoll_ctl(epfd, EPOLL_CTL_DEL, server_fd, NULL);
  close(server_fd);
  close(epfd);
  return 0;
}

