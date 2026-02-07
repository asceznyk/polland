#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>

#include "defs.h"
#include "utils.h"
#include "client.h"
#include "http.h"

void client_free(struct client_state *client) {
  buffer_free(&client->in_headers);
  buffer_free(&client->in_body);
  buffer_free(&client->out_headers);
  buffer_free(&client->out_body);
}

bool client_init(struct client_state *client, int fd) {
  *client = (struct client_state){
    .fd = fd,
    .state = STATE_READING_HEADERS,
    .in_has_body = false,
  };
  if (!buffer_init(&client->in_headers, BUFFER_SIZE)) goto fail;
  if (!buffer_init(&client->in_body, BUFFER_SIZE)) goto fail;
  if (!buffer_init(&client->out_headers, BUFFER_SIZE)) goto fail;
  if (!buffer_init(&client->out_body, BUFFER_SIZE)) goto fail;
  return true;
  fail:
    client_free(client);
    return false;
}

bool client_epoll_register(int epfd, int client_fd) {
  struct client_state *client = calloc(1, sizeof(*client));
  if(!client) {
    close(client_fd);
    return false;
  }
  if(!client_init(client, client_fd)) return false;
  struct epoll_event evt = {0};
  evt.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  evt.data.ptr = client;
  int flags = fcntl(client_fd, F_GETFL, 0);
  fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, client->fd, &evt) < 0) {
    perror("epoll_ctl");
    client_close_and_free(epfd, client);
    return false;
  }
  return true;
}

void client_accept_conn(int epfd, int server_fd) {
  for (;;) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      else
        perror("accept");
      break;
    }
    if(!client_epoll_register(epfd, client_fd)) break;
  }
}

void client_close_and_free(int epfd, struct client_state *client) {
  printf("client_close_and_free, fd = %d\n", client->fd);
  epoll_ctl(epfd, EPOLL_CTL_DEL, client->fd, NULL);
  close(client->fd);
  client_free(client);
  free(client);
}

void client_epoll_switch_state(
  int epfd, struct client_state *client, bool add_write
) {
  printf("client_epoll_switch_state, fd = %d\n", client->fd);
  struct epoll_event evt = {0};
  uint32_t events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  if (add_write)
    events |= EPOLLOUT;
  evt.events = events;
  evt.data.ptr = client;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, client->fd, &evt) < 0) {
    perror("epoll_ctl MOD");
    client_close_and_free(epfd, client);
  }
}

void client_http_adv_state(struct client_state *client) {
  if (client->state == STATE_READING_HEADERS) {
    client->state = client->in_has_body ?
      STATE_READING_BODY :
      STATE_WRITING_HEADERS;
  } else if (client->state == STATE_READING_BODY) {
    client->state = STATE_WRITING_HEADERS;
  } else if (client->state == STATE_WRITING_HEADERS) {
    client->state = STATE_WRITING_BODY;
  } else if (client->state == STATE_WRITING_BODY) {
    client->state = STATE_READING_HEADERS; //keep-alive
  }
}

void client_split_after_headers(
  struct client_state *client, size_t hdr_end
) {
  struct buffer *headers = &client->in_headers;
  size_t extra = headers->len - hdr_end;
  if (extra == 0) {
    headers->len = hdr_end;
    return;
  }
  if (client->state == STATE_READING_BODY) {
    buffer_append(&client->in_body, headers->data + hdr_end, extra);
    headers->len = hdr_end;
  } else {
    buffer_consume(headers, hdr_end);
  }
}

struct buffer *client_current_in_buffer(struct client_state *c) {
  switch (c->state) {
    case STATE_READING_HEADERS:
      return &c->in_headers;
    case STATE_READING_BODY:
      return &c->in_body;
    default:
      return NULL;
  }
}

void client_handle_read(int epfd, struct client_state *client) {
  struct buffer *buf = client_current_in_buffer(client);
  if (!buf) {
    return;
  }
  for (;;) {
    ssize_t n = recv(
      client->fd,
      buf->data + buf->len,
      buf->cap - buf->len,
      0
    );
    if (n == 0) {
      printf("client connection closed on read! n == 0\n");
      client_close_and_free(epfd, client);
      return;
    }
    if (n < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      client_close_and_free(epfd, client);
      return;
    }
    buf->len += n;
    if (client->state == STATE_READING_HEADERS) {
      ssize_t hdr_end = find_double_crlf(buf->data, buf->len, 0);
      if (hdr_end != -1) {
        client_http_adv_state(client);
        client_split_after_headers(client, hdr_end);
        if (client->state == STATE_WRITING_HEADERS) {
          http_build_out_response(client, hdr_end);
          print_client_io_buffers(client);
          client_epoll_switch_state(epfd, client, 1);
          return;
        }
      }
    } /*else if (client->state == STATE_READING_BODY) {
      if (client->in_body.len >= client->content_length) {
        client_http_adv_state(client);
        client_epoll_switch_state(epfd, client, 1);
        return;
      }
    }*/
  }
}

static int client_send_buffer(
  int fd, struct buffer *buf, size_t *sent
) {
  while (*sent < buf->len) {
    size_t remaining = buf->len - *sent;
    size_t to_send = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
    ssize_t n = send(fd, buf->data + *sent, to_send, 0);
    if (n > 0) {
      *sent += n;
      continue;
    }
    if (n == 0) return -1;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -1;
  }
  return 1;
}

void client_reset_out_buffers(struct client_state *client) {
  printf("client_reset_out_buffers fd = %d\n", client->fd);
  struct buffer *out_headers = &client->out_headers;
  struct buffer *out_body = &client->out_body;
  printf("out_headers free!\n");
  free(out_headers->data);
  out_headers->data = NULL;
  out_headers->len = 0;
  out_headers->cap = 0;
  printf("out_body free!\n");
  free(out_body->data);
  out_body->data = NULL;
  out_body->len = 0;
  out_body->cap = 0;
}

void client_handle_write(int epfd, struct client_state *client) {
  for (;;) {
    int rc;
    if (client->state == STATE_WRITING_HEADERS) {
      rc = client_send_buffer(
        client->fd,
        &client->out_headers,
        &client->out_headers_sent
      );
      if (rc < 0) goto fail;
      if (rc == 0) return; // EAGAIN → wait for EPOLLOUT
      client->out_headers_sent = 0;
      client_http_adv_state(client); // → WRITING_BODY
      continue;
    }
    if (client->state == STATE_WRITING_BODY) {
      rc = client_send_buffer(
        client->fd,
        &client->out_body,
        &client->out_body_sent
      );
      if (rc < 0) goto fail;
      if (rc == 0) return; // EAGAIN → wait
      client->out_body_sent = 0;
      client_http_adv_state(client); // → READING_HEADERS
      continue;
    }
    if (client->state == STATE_READING_HEADERS) {
      client_reset_out_buffers(client);
      client_epoll_switch_state(epfd, client, 0);
      return;
    }
    return;
  }
fail:
  client_reset_out_buffers(client);
  client_close_and_free(epfd, client);
}

