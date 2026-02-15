#include "utils.h"
#include "client.h"
#include "http.h"

void client_io_buffers_free(struct client_state *client) {
  buffer_free(&client->in_headers);
  buffer_free(&client->in_body);
  buffer_free(&client->out_headers);
  buffer_free(&client->out_body);
}

bool client_init(struct client_state *client, int fd) {
  *client = (struct client_state){
    .fd = fd,
    .state = CLIENT_READING_HEADERS,
    .in_has_body = false,
    .in_header_end = 0,
    .in_body_end = 0,
    .out_file_offset = 0,
    .out_body_kind = BODY_BUFFER,
  };
  backend_init(&client->backend);
  if (!buffer_init(&client->in_headers, BUFFER_SIZE)) goto fail;
  if (!buffer_init(&client->in_body, BUFFER_SIZE)) goto fail;
  if (!buffer_init(&client->out_headers, BUFFER_SIZE)) goto fail;
  if (!buffer_init(&client->out_body, BUFFER_SIZE)) goto fail;
  client->client_ctx.kind = FD_CLIENT;
  client->client_ctx.client = client;
  client->client_ctx.fd = fd;
  return true;
  fail:
    client_io_buffers_free(client);
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
  evt.data.ptr = &client->client_ctx;
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
  printf("client_close_and_free: client_fd = %d\n", client->fd);
  epoll_ctl(epfd, EPOLL_CTL_DEL, client->fd, NULL);
  close(client->fd);
  client_io_buffers_free(client);
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
  evt.data.ptr = &client->client_ctx;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, client->fd, &evt) < 0) {
    perror("epoll_ctl MOD");
    client_close_and_free(epfd, client);
  }
}

void client_http_adv_state(struct client_state *client) {
  if (client->state == CLIENT_READING_HEADERS) {
    client->state = client->in_has_body ?
      CLIENT_READING_BODY :
      CLIENT_WRITING_HEADERS;
  } else if (client->state == CLIENT_READING_BODY) {
    client->state = CLIENT_WRITING_HEADERS;
  } else if (client->state == CLIENT_WRITING_HEADERS) {
    client->state = CLIENT_WRITING_BODY;
  } else if (client->state == CLIENT_WRITING_BODY) {
    client->state = CLIENT_READING_HEADERS; //keep-alive
  }
}

void client_split_after_headers(struct client_state *client, size_t hdr_end) {
  printf("client_split_after_headers: reached!\n");
  struct buffer *headers = &client->in_headers;
  if (client->state != CLIENT_READING_BODY) {
    buffer_consume(headers, hdr_end);
    return;
  }
  size_t extra = headers->len - hdr_end;
  if (extra == 0) {
    headers->len = hdr_end;
    return;
  }
  buffer_append(&client->in_body, headers->data + hdr_end, extra);
  headers->len = hdr_end;
}

struct buffer *client_current_in_buffer(struct client_state *client) {
  switch (client->state) {
    case CLIENT_READING_HEADERS:
      return &client->in_headers;
    case CLIENT_READING_BODY:
      return &client->in_body;
    default:
      return NULL;
  }
}

bool client_backend_connect(int epfd, struct client_state *client) {
  printf("client_backend_connect: reached!\n");
  if (backend_connect(&client->backend, BE_HOST, BE_PORT) < 0) {
    backend_close(epfd, &client->backend);
    return false;
  }
  return backend_epoll_register(epfd, client);
}

void client_handle_read(int epfd, struct client_state *client) {
  struct buffer *buf = client_current_in_buffer(client);
  if (!buf) return;
  printf("client_handle_read: buf->len = %ld\n", buf->len);
  printf("client_handle_read: client_fd = %d\n", client->fd);
  for (;;) {
    assert(buf->len <= buf->cap);
    ssize_t n = recv(
      client->fd,
      buf->data + buf->len,
      buf->cap - buf->len,
      0
    );
    if (buf->len > buf->cap) printf("error! buf->len = %ld buf->cap = %ld\n", buf->len, buf->cap);
    if (n == 0) {
      printf("buf->len = %ld buf->cap = %ld\n", buf->len, buf->cap);
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
    if (client->state == CLIENT_READING_HEADERS) {
      ssize_t hdr_end = find_double_crlf(buf->data, buf->len, 0);
      if (hdr_end == -1) continue;
      client->in_header_end = (size_t)hdr_end;
      client_http_adv_state(client);
      if (client->state != CLIENT_WRITING_HEADERS) continue;
      print_client_in_buffers(client);
      http_build_out_resp(client, hdr_end);
      print_client_out_buffers(client);
      if (client->in_url_is_static) {
        client_split_after_headers(client, hdr_end);
        client_epoll_switch_state(epfd, client, 1);
        client->in_url_is_static = false;
        return;
      }
      if(!client_backend_connect(epfd, client)) {
        http_build_err_resp(
          client, BAD_GATEWAY_HEADER, BAD_GATEWAY_BODY, false
        );
        client_split_after_headers(client, hdr_end);
        client_epoll_switch_state(epfd, client, 1);
        return;
      }
    } /*else if (client->state == CLIENT_READING_BODY) {
      if (client->in_body.len >= client->content_length) {
        client_http_adv_state(client);
        client_epoll_switch_state(epfd, client, 1);
        return;
      }
    }*/
  }
}

void client_reset_out_headers(struct client_state *client) {
  client->out_headers_sent = 0;
  client->out_headers.len = 0;
}

void client_reset_out_body_or_file(struct client_state *client) {
  switch (client->out_body_kind) {
    case BODY_FILE:
      if (client->out_file_fd != -1) close(client->out_file_fd);
      client->out_file_fd = -1;
      client->out_file_size = 0;
      client->out_file_offset = 0;
      client->out_body_kind = BODY_BUFFER;
      break;
    case BODY_BUFFER:
      client->out_body_sent = 0;
      client->out_body.len = 0;
      client->out_body_kind = BODY_BUFFER;
      break;
    case BODY_NONE:
    default:
      break;
  }
}

void client_handle_write(int epfd, struct client_state *client) {
  for (;;) {
    int rc;
    if (client->state == CLIENT_WRITING_HEADERS) {
      rc = buffer_send_flat(
        client->fd,
        &client->out_headers,
        &client->out_headers_sent
      );
      if (rc < 0) goto fail;
      if (rc == 0) return; // EAGAIN → wait for EPOLLOUT
      client_reset_out_headers(client);
      client_http_adv_state(client); // → WRITING_BODY
      continue;
    }
    if (client->state == CLIENT_WRITING_BODY) {
      printf("client->out_body_kind == %d\n", client->out_body_kind);
      if (client->out_body_kind == BODY_FILE) {
        rc = buffer_send_file(
          client->fd,
          client->out_file_fd,
          &client->out_file_offset,
          client->out_file_size
        );
      } else if (client->out_body_kind == BODY_BUFFER) {
        rc = buffer_send_flat(
          client->fd,
          &client->out_body,
          &client->out_body_sent
        );
      } else rc = 1;
      if (rc < 0) goto fail;
      if (rc == 0) return; // EAGAIN → wait
      client_reset_out_body_or_file(client);
      client_http_adv_state(client); // → READING_HEADERS
      continue;
    }
    if (client->state == CLIENT_READING_HEADERS) {
      client_epoll_switch_state(epfd, client, 0);
      return;
    }
    return;
  }
fail:
  client_close_and_free(epfd, client);
}

