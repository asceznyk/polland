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
  printf("handle_interrupt: signal recieved %d\n", sig);
  printf("handle_interrupt: SIGINT/SIGTERM recieved, stopping the server..\n");
  running = 0;
}

backend_t *backend_pool = NULL;
int client_req_count = 0;

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
  if(listen(server_fd, MAX_BACKLOG) < 0) {
    perror("failed to listen!\n");
    exit(EXIT_FAILURE);
  }
  printf("main: server is listening on port %d\n", server_cfg.port);
  int epfd = epoll_create1(0);
  struct epoll_event evt = {
    .events = EPOLLIN,
    .data.fd = server_fd
  };
  epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &evt);
  struct epoll_event sevents[MAX_EVENTS];
  int flags = fcntl(server_fd, F_GETFL, 0);
  fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
  client_t *free_client_head = NULL;
  backend_t *free_backend_head = NULL;
  backend_pool = backend_build_pool(epfd, MIN_UPSTREAM_CONNECTIONS);
  while (running) {
    printf("main: epoll_wait?!\n");
    int n = epoll_wait(epfd, sevents, MAX_EVENTS, -1);
    printf("main: "); backend_show_registry();
    free_client_head = NULL;
    printf("main: n = %d\n", n);
    for (int i = 0; i < n; i++) {
      uint32_t events = sevents[i].events;
      if (sevents[i].data.fd == server_fd) {
        client_accept_conn(epfd, server_fd);
        continue;
      }
      struct fd_ctx *ctx = sevents[i].data.ptr;
      printf("main: ctx->kind = %d, ctx->fd = %d\n", ctx->kind, ctx->fd);
      printf("main: ctx->peer %p\n", ctx->peer);
      if (ctx->closing) {
        printf("main: ctx marked as closing!\n");
        continue;
      }
      int dead_client = 0, dead_backend = 0;
      if (events & (EPOLLERR | EPOLLHUP)) {
        printf("main: EPOLLERR | EPOLLHUP\n");
        if (ctx->kind == FD_BACKEND) {
          backend_t *backend = (backend_t *)ctx->peer;
          backend_handle_err(epfd, backend);
          //dead_backend = 1;
        } else {
          client_t *client = (client_t *)ctx->peer;
          client_mark_closing(client);
          dead_client = 1;
        }
      } else if (ctx->kind == FD_CLIENT) {
        client_t *client = (client_t *)ctx->peer;
        if (events & (EPOLLIN | EPOLLRDHUP)) {
          if (client_handle_read(epfd, client) < 0) dead_client = 1;
        }
        if (events & EPOLLOUT) {
          if (client_handle_write(epfd, client) < 0) dead_client = 1;
        }
      } else if (ctx->kind == FD_BACKEND) {
        backend_t *backend = (backend_t *)ctx->peer;
        if (events & (EPOLLIN | EPOLLHUP)) {
          if (backend_handle_read(epfd, backend) < 0) dead_backend = 1;
        }
        if (events & EPOLLOUT) {
          if(backend_handle_write(epfd, backend) < 0) dead_backend = 1;
        }
      }
      if (dead_client) {
        client_t *client = (client_t *)ctx->peer;
        client->next_to_free = free_client_head;
        free_client_head = client;
      }
      if (dead_backend) {
        backend_t *backend = (backend_t *)ctx->peer;
        backend->next_to_free = free_backend_head;
        free_backend_head = backend;
      }
    }
    while (free_client_head) {
      printf("main: freeing client fd = %d!\n", free_client_head->fd);
      client_t *next = free_client_head->next_to_free;
      client_destroy(epfd, free_client_head);
      free_client_head = next;
    }
    while (free_backend_head) {
      printf("main: freeing backend fd = %d!\n", free_backend_head->fd);
      backend_t *next = free_backend_head->next_to_free;
      backend_destroy(epfd, free_backend_head);
      free_backend_head = next;
    }
    printf("main: end of iteration!\n");
  }
  printf("main: closing all clients and main server thread...\n");
  epoll_ctl(epfd, EPOLL_CTL_DEL, server_fd, NULL);
  close(server_fd);
  close(epfd);
  return 0;
}

