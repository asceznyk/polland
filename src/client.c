#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>

#include "defs.h"
#include "utils.h"
#include "client.h"
#include "http_response.h"

void close_and_free_client(struct client_state *client) {
  epoll_ctl(epfd, EPOLL_CTL_DEL, client->fd, NULL);
  close(client->fd);
  free(client);
}

void register_client(int epfd, int client_fd) {
  struct client_state *client = calloc(1, sizeof(*client));
  if(!client) {
    close(client_fd);
    return;
  }
  client->fd = client_fd;
  client->state = STATE_READING;
  client->in_pos = 0;
  client->in_len = 0;
  struct epoll_event evt = {0};
  evt.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  evt.data.ptr = client;
  int flags = fcntl(client_fd, F_GETFL, 0);
  fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, client->fd, &evt) < 0) {
    perror("epoll_ctl");
    close_and_free_client(client);
  }
}

void add_client_conn(int epfd, int server_fd) {
  for (;;) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      else
        perror("accept");
      break;
    }
    register_client(epfd, client_fd);
  }
}

void switch_rw_state(int epfd, struct client_state *client, bool add_write) {
  struct epoll_event evt = {0};
  uint32_t events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  if (add_write)
    events |= EPOLLOUT;
  evt.events = events;
  evt.data.ptr = client;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, client->fd, &evt) < 0) {
    perror("epoll_ctl MOD");
    close_and_free_client(client);
  }
}

void handle_client_read(int epfd, struct client_state *client) {
  for (;;) {
    ssize_t bytes_read = recv(
      client->fd,
      client->in_buf + client->in_len,
      BUFFER_SIZE - client->in_len, 0
    );
    if (bytes_read == 0) {
      printf("client connection closed!, bytes_read == 0\n");
      close_and_free_client(client);
      break;
    }
    if (bytes_read < 0) {
      if (errno == EINTR) continue;
      else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      else {
        printf("client connection closed!, ohh shit!\n");
        close_and_free_client(client);
        break;
      }
    }
    client->in_len += bytes_read;
    printf(
      "client->in_len = %ld, client->in_pos = %ld\t",
      client->in_len, client->in_pos
    );
    print_escaped(client->in_buf, client->in_len);
    if (ends_with_double_crlf(client->in_buf, client->in_len)) {
      build_http_response(client);
      client->in_pos = client->in_len;
      switch_rw_state(epfd, client, 1);
      printf("client request recieved!\n");
      break;
    }
  }
  printf("EXIT handle_client_read..\n");
}

void reset_client_out_buf(struct client_state *client) {
  free(client->out_buf);
  client->out_buf = NULL;
  client->out_len = 0;
  client->out_sent = 0;
  printf("reset!!!\n");
  printf("client->fd = %d, client->out_len = %ld\n", client->fd, client->out_len);
}

void handle_client_write(int epfd, struct client_state *client) {
  printf("fd = %d, out_len = %ld\n", client->fd, client->out_len);
  while (client->out_sent < client->out_len) {
    size_t remaining = client->out_len - client->out_sent;
    size_t to_send = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
    printf("remaining = %ld, to_send = %ld\n", remaining, to_send);
    //print_escaped(client->out_buf, to_send);
    ssize_t bytes_written = send(
      client->fd,
      client->out_buf + client->out_sent,
      to_send,
      0
    );
    printf("bytes_written = %ld\n", bytes_written);
    if (bytes_written == 0) {
      printf("client connection closed!, bytes_written == 0\n");
      close_and_free_client(client);
      break;
    }
    if (bytes_written > 0) {
      client->out_sent += bytes_written;
    }
    else {
      printf("bytes_written < 0?\n");
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      close_and_free_client(client);
      return;
    }
  }
  reset_client_out_buf(client);
  switch_rw_state(epfd, client, 0);
  printf("EXIT handle_client_write..\n");
}

