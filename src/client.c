#include "config.h"
#include "utils.h"
#include "backend.h"
#include "client.h"
#include "http.h"

void client_io_buffers_free(struct client_state *client) {
  buffer_free(&client->in_stream);
  buffer_free(&client->out_stream);
}

bool client_init(struct client_state *client, int fd) {
  *client = (struct client_state){
    .fd = fd,
    .closing = false,
    .in_state = CLIENT_READING_HEADERS,
    .out_state = CLIENT_WRITING_HEADERS,
    .in_has_body = false,
    .out_file_fd = -1,
    .out_file_offset = 0,
    .out_body_kind = BODY_BUFFER,
  };
  backend_init(&client->backend);
  if (!buffer_init(&client->in_stream, BUFFER_SIZE)) goto fail;
  if (!buffer_init(&client->out_stream, BUFFER_SIZE)) goto fail;
  client->client_ctx.closing = false;
  client->client_ctx.kind = FD_CLIENT;
  client->client_ctx.client = client;
  return true;
  fail:
    client_io_buffers_free(client);
    return false;
}

void client_mark_closing(struct client_state *client) {
  printf("client_mark_closing: client_fd = %d\n", client->fd);
  if (client->closing) return;
  client->backend_ctx.closing = true;
  client->client_ctx.closing = true;
  client->closing = true;
}

void client_destroy(int epfd, struct client_state *client) {
  printf("client_destroy: reached\n");
  if (client->fd != -1) {
    printf("client_destroy: closing client->fd = %d!\n", client->fd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    client->fd = -1;
  }
  if (client->backend.fd != -1) {
    printf("client_destroy: backend_close!\n");
    backend_close(epfd, &client->backend);
  }
  client_io_buffers_free(client);
  client->client_ctx.client = NULL;
  client->backend_ctx.client = NULL;
  free(client);
}

bool client_epoll_register(int epfd, int client_fd) {
  struct client_state *client = calloc(1, sizeof(*client));
  if(!client) {
    close(client_fd);
    return false;
  }
  if(!client_init(client, client_fd)) return false;
  struct epoll_event evt = {0};
  evt.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  evt.data.ptr = &client->client_ctx;
  int flags = fcntl(client_fd, F_GETFL, 0);
  fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, client->fd, &evt) < 0) {
    perror("epoll_ctl");
    client_mark_closing(client);
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
    if (!client_epoll_register(epfd, client_fd)) break;
  }
}

bool client_backend_connect(int epfd, struct client_state *client) {
  printf("client_backend_connect: reached!\n");
  if (client->backend.fd != -1) backend_close(epfd, &client->backend);
  if (backend_connect(
    &client->backend,
    server_cfg.upstream.host,
    server_cfg.upstream.port
  ) < 0) {
    backend_close(epfd, &client->backend);
    return false;
  }
  return backend_epoll_register(epfd, client);
}

void client_adv_in_state(struct client_state *client) {
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

int client_handle_read(int epfd, struct client_state *client) {
  printf("client_handle_read: reached! client->in_state = %d\n", client->in_state);
  struct buffer *buf = &client->in_stream;
  if (!buf) {
    printf("client_handle_read: !buf\n");
    return 0;
  };
  if (client->closing) {
    printf("client_handle_read: client->closing = %d\n", client->closing);
    return -1;
  }
  printf("client_handle_read: "); print_client_in_stream(client);
  printf("client_handle_read: client->fd = %d, buf->len = %ld\n", client->fd, buf->len);
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
        return 0;
      }
      client_mark_closing(client);
      return -1;
    }
    buf->len += n;
    printf("client_handle_read: "); print_client_in_stream(client);
    if (client->in_state == CLIENT_REQ_COMPLETE) {
      printf("client_handle_read: CLIENT_REQ_COMPLETE! advancing...\n");
      client_adv_in_state(client);
    }
    if (client->in_state == CLIENT_READING_HEADERS) {
      ssize_t hdr_end = find_double_crlf(buf->data, buf->len, 0);
      if (hdr_end == -1) continue;
      client->is_http_one_point_o = http_is_one_point_o(client->in_stream.data, client->in_stream.len);
      client->in_header_end = (size_t)hdr_end;
      client_adv_in_state(client);
      if (client->in_state != CLIENT_REQ_COMPLETE) continue;
      http_build_out_resp(client, hdr_end);
      if (client->in_url_is_static) {
        buffer_consume(&client->in_stream, hdr_end);
        client->in_url_is_static = false;
        continue;
      }
      if(!client_backend_connect(epfd, client)) {
        http_build_err_resp(
          client, BAD_GATEWAY_HEADER, BAD_GATEWAY_BODY, false
        );
        buffer_consume(&client->in_stream, hdr_end);
        continue;
      }
    } //TODO: CLIENT_READING_BODY
  }
  return 1;
}

void client_reset_out_streams(struct client_state *c) {
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

void client_adv_out_state(struct client_state *client) {
  if (client->out_state == CLIENT_WRITING_HEADERS) {
    client->out_state = CLIENT_WRITING_BODY;
  } else if (client->out_state == CLIENT_WRITING_BODY) {
    client->out_state = CLIENT_RESP_COMPLETE;
  } else if (client->out_state == CLIENT_RESP_COMPLETE) {
    client->out_state = CLIENT_WRITING_HEADERS;
  }
}

int client_handle_write(int epfd, struct client_state *client) {
  printf("client_handle_write: called! client->out_state = %d\n", client->closing, client->out_state);
  if (client->closing) return -1;
  for (;;) {
    int rc;
    if (client->out_state == CLIENT_RESP_COMPLETE) {
      if (client->is_http_one_point_o) goto fail;
      client_adv_out_state(client);
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
        rc = buffer_send_flat(client->fd, &client->out_stream, &client->out_sent);
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

