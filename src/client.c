#include "config.h"
#include "utils.h"
#include "backend.h"
#include "client.h"
#include "http.h"

void client_io_buffers_free(client_t *client) {
  printf("client_io_buffers_free: reached\n");
  buffer_free(&client->in_stream);
  buffer_free(&client->out_stream);
}

bool client_init(client_t *client, int fd) {
  *client = (client_t) {
    .fd = fd,
    .closing = false,
    .req_len = 0,
    .in_state = CLIENT_READING_HEADERS,
    .out_state = CLIENT_WRITING_HEADERS,
    .in_has_body = false,
    .out_file_fd = -1,
    .out_file_offset = 0,
    .out_body_kind = BODY_BUFFER,
  };
  if (!buffer_init(&client->in_stream, BUFFER_SIZE)) goto fail;
  if (!buffer_init(&client->out_stream, BUFFER_SIZE)) goto fail;
  printf("client_init: inited buffers!\n");
  client->ctx.closing = false;
  client->ctx.kind = FD_CLIENT;
  client->ctx.peer = client;
  return true;
  fail:
    client_io_buffers_free(client);
    return false;
}

void client_mark_closing(client_t *client) {
  printf("client_mark_closing: client->fd = %d\n", client->fd);
  if (client->ctx.closing || client->closing) return;
  client->ctx.closing = true;
  client->closing = true;
}

void client_destroy(int epfd, client_t *client) {
  printf("client_destroy: reached\n");
  if (client->fd != -1) {
    printf("client_destroy: closing client->fd = %d!\n", client->fd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    client->fd = -1;
  }
  if (client->backend && client->backend->fd != -1) {
    printf("client_destroy: has a backend! destroying backend\n");
    backend_t *backend = client->backend;
    backend_detach_client(client, backend);
    backend_destroy(epfd, backend);
  }
  client_io_buffers_free(client);
  client->ctx.peer = NULL;
  free(client);
}

client_t *client_create(int fd) {
  client_t *client = malloc(sizeof(*client));
  if(!client) {
    close(fd);
    return NULL;
  }
  if(!client_init(client, fd)) return NULL;
  return client;
}

bool client_epoll_register(int epfd, client_t *client) {
  struct epoll_event evt = {0};
  evt.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  evt.data.ptr = &client->ctx;
  int flags = fcntl(client->fd, F_GETFL, 0);
  fcntl(client->fd, F_SETFL, flags | O_NONBLOCK);
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, client->fd, &evt) < 0) {
    perror("epoll_ctl");
    client_mark_closing(client);
    return false;
  }
  return true;
}

void client_epoll_toggle_write(
  int epfd, client_t *client, bool add_write
) {
  printf("client_epoll_toggle_write: fd = %d, add_write = %d\n", client->fd, add_write);
  struct epoll_event evt = {0};
  uint32_t events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  if (add_write) events |= EPOLLOUT;
  evt.events = events;
  evt.data.ptr = &client->ctx;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, client->fd, &evt) < 0) {
    perror("epoll_ctl MOD");
    client_mark_closing(client);
  }
}

void client_accept_conn(int epfd, int server_fd) {
  for (;;) {
    int fd = accept(server_fd, NULL, NULL);
    if (fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      else
        perror("accept");
      break;
    }
    client_t *client = client_create(fd);
    if (!client) break;
    if (!client_epoll_register(epfd, client)) break;
  }
}

bool client_backend_connect(int epfd, client_t *client) {
  if (client->backend && client->backend->fd != 0) {
    backend_epoll_toggle_write(epfd, client->backend, 1);
    return true;
  }
  int fd = -1;
  if((
    fd = backend_connect(server_cfg.upstream.host, server_cfg.upstream.port)
  ) < 0) return false;
  backend_t *backend = backend_create(fd);
  backend_attach_client(client, backend);
  return backend_epoll_register(epfd, backend);
}

void client_adv_in_state(client_t *client) {
  if (client->in_state == CLIENT_READING_HEADERS) {
    client->in_state = client->in_has_body ?
      CLIENT_READING_BODY :
      CLIENT_REQ_COMPLETE;
  } else if (client->in_state == CLIENT_READING_BODY) {
    client->in_state = CLIENT_REQ_COMPLETE;
  } else if (client->in_state == CLIENT_REQ_COMPLETE) {
    client->in_state = CLIENT_READING_HEADERS;
  }
}

int client_process_in_stream(int epfd, client_t *client) {
  struct buffer *buf = &client->in_stream;
  size_t bytes_read = 0;
  size_t len_buf = buf->len;
  printf("client_process_in_stream: before_loop: bytes_read = %ld, len_buf = %ld\n", bytes_read, len_buf);
  while (bytes_read <= len_buf) {
    if (client->in_state == CLIENT_REQ_COMPLETE) {
      printf("client_process_in_stream: CLIENT_REQ_COMPLETE! advancing...\n");
      client_adv_in_state(client);
      break;
    }
    if (client->in_state == CLIENT_READING_HEADERS) {
      ssize_t hdr_end = find_double_crlf(buf->data, buf->len, 0);
      if (hdr_end == -1) break;
      client_adv_in_state(client);
      client->req_len = (size_t)hdr_end;
      bytes_read += client->req_len;
      if (client->in_state != CLIENT_REQ_COMPLETE) continue;
      client->is_http_one_point_o = http_is_one_point_o(
        client->in_stream.data, client->req_len
      );
      http_build_out_resp(client, client->req_len);
      if (client->in_url_is_static) {
        buffer_consume(&client->in_stream, client->req_len);
        client->in_url_is_static = false;
        continue;
      }
      if (!client_backend_connect(epfd, client)) {
        http_build_err_resp(
          client, BAD_GATEWAY_HEADER, BAD_GATEWAY_BODY, false
        );
        buffer_consume(&client->in_stream, client->req_len);
      }
    } //TODO: CLIENT_READING_BODY
  }
  printf("client_process_in_stream: after_loop: bytes_read = %ld\n", bytes_read);
  client_epoll_toggle_write(epfd, client, 1);
  return 0;
}

bool client_response_busy(client_t *client) {
  return client->out_stream.len > 0 && client->out_state == CLIENT_WRITING_HEADERS;
}

int client_handle_read(int epfd, client_t *client) {
  printf("client_handle_read: reached!\n");
  struct buffer *buf = &client->in_stream;
  if (!buf) return 0;
  if (client->closing) return -1;
  for (;;) {
    assert(buf->len <= buf->cap);
    ssize_t n = recv(
      client->fd,
      buf->data + buf->len,
      buf->cap - buf->len,
      0
    );
    if (buf->len > buf->cap)
      printf("client_handle_read: error! buf->len = %ld buf->cap = %ld\n", buf->len, buf->cap);
    if (n == 0) {
      printf("client_handle_read: client connection closed on read! n == 0\n");
      client_mark_closing(client);
      return -1;
    }
    if (n < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        printf("client_handle_read: EAGAIN\n");
        return client_process_in_stream(epfd, client);
      }
      client_mark_closing(client);
      return -1;
    }
    buf->len += n;
    printf("client_handle_read: "); print_client_in_stream(client);
  }
  return 1;
}

void client_reset_out_streams(client_t *c) {
  c->out_stream.len = 0;
  c->out_sent = 0;
  if (c->out_file_fd != -1) {
    close(c->out_file_fd);
    c->out_file_fd = -1;
  }
  c->out_file_offset = 0;
  c->out_file_size = 0;
  c->out_body_kind = BODY_BUFFER;
}

void client_adv_out_state(client_t *client) {
  if (client->out_state == CLIENT_WRITING_HEADERS) {
    client->out_state = CLIENT_WRITING_BODY;
  } else if (client->out_state == CLIENT_WRITING_BODY) {
    client->out_state = CLIENT_RESP_COMPLETE;
  } else if (client->out_state == CLIENT_RESP_COMPLETE) {
    client->out_state = CLIENT_WRITING_HEADERS;
  }
}

int client_handle_write(int epfd, client_t *client) {
  printf("client_handle_write: called!\n");
  if (client->closing) return -1;
  for (;;) {
    int rc;
    if (client->out_state == CLIENT_RESP_COMPLETE) {
      if (client->is_http_one_point_o) goto fail;
      client_adv_out_state(client);
      printf("client_handle_write: client->in_stream.len = %ld\n", client->in_stream.len);
      if (!client->is_http_one_point_o && client->in_stream.len > 0) {
        return client_process_in_stream(epfd, client);
      }
      client_epoll_toggle_write(epfd, client, 0);
      return 0;
    }
    if (
      client->out_state == CLIENT_WRITING_HEADERS ||
      client->out_state == CLIENT_WRITING_BODY
    ) {
      printf("client_handle_write: client->out_sent = %ld, client->out_body_kind = %d\n", client->out_sent, client->out_body_kind);
      rc = 1;
      if (client->out_body_kind == BODY_FILE) {
        printf("client_handle_write:  CLIENT_WRITING_BODY, BODY_FILE \n");
        printf("client_handle_write: client->out_file_fd = %d\n", client->out_file_fd);
        rc = buffer_send_flat(
          client->fd,
          &client->out_stream,
          client->out_stream.len,
          &client->out_sent
        );
        if (rc < 0) goto fail;
        if (rc == 0) return 0;
        printf("client_handle_write: client->out_sent = %ld\n", client->out_sent);
        rc = buffer_send_file(
          client->fd,
          client->out_file_fd,
          &client->out_file_offset,
          client->out_file_size
        );
      } else {
        printf("client_handle_write: "); print_client_out_stream(client);
        printf("client_handle_write: client->out_stream.len = %ld\n", client->out_stream.len);
        rc = buffer_send_flat(
          client->fd,
          &client->out_stream,
          client->out_stream.len,
          &client->out_sent
        );
      }
      if (rc < 0) goto fail;
      if (rc == 0) return 0; // EAGAIN → wait
      client_reset_out_streams(client);
      client_adv_out_state(client);
      if (client->is_http_one_point_o) goto fail;
      continue;
    }
    return 0;
  }
fail:
  client_mark_closing(client);
  return -1;
}

