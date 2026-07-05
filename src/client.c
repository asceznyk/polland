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
    .backend = NULL,
    .in_state = CLIENT_READING_HEADERS,
    .out_state = CLIENT_WRITING_RESP,
    .out_file_fd = -1,
    .out_file_offset = 0,
    .out_body_kind = BODY_BUFFER,
  };
  if (!transaction_init(&client->transaction)) goto fail;
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
    printf("client_destroy: backend present %p\n", client->backend);
    backend_t *backend = client->backend;
    backend_return_client(backend);
  }
  client_io_buffers_free(client);
  client->ctx.peer = NULL;
  printf("client_destroy: destroying %p!\n", client);
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
  evt.events = EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
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
  uint32_t events = EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
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

bool client_setup_backend(int epfd, backend_t **backend) {
  printf("client_setup_backend!\n");
  int fd = backend_connect(
    server_cfg.upstream.host,
    server_cfg.upstream.port
  );
  if (fd < 0)
    return false;
  if (*backend == NULL) {
    *backend = backend_create(fd);
    if (*backend == NULL) {
      close(fd);
      return false;
    }
  } else {
    (*backend)->fd = fd;
  }
  return backend_epoll_register(epfd, *backend);
}

bool client_borrow_backend(int epfd, client_t *client) {
  printf("client_borrow_backend: reached!\n");
  if (client->backend) {
    printf("client_borrow_backend: HAS EXISTTING BACKEND!\n");
    backend_epoll_toggle_write(epfd, client->backend, 1);
    return true;
  }
  backend_t *backend = NULL;
  printf("client_borrow_backend: backend_pool = %p\n", backend_pool);
  if (!backend_pool) {
    if (!client_setup_backend(epfd, &backend)) return false;
  } else {
    backend = backend_detach_from_pool();
    printf("client_borrow_backend: backend->fd = %d\n", backend->fd);
    if (backend->fd == -1) {
      if (!client_setup_backend(epfd, &backend)) return false;
    }
  }
  printf("client_borrow_backend: backend = %p\n", backend);
  backend_attach_client(client, backend);
  backend_epoll_toggle_write(epfd, backend, 1);
  return true;
}

int client_process_in_stream(int epfd, client_t *client) {
  printf("client_process_in_stream: called!\n");
  if (client->backend) return 0;
  buffer_t *buf = &client->in_stream;
  transaction_t *transaction = &client->transaction;
  size_t bytes_read = 0;
  size_t len_buf = buf->len;
  printf("client_process_in_stream: len_buf = %ld\n", len_buf);
  while (bytes_read <= len_buf) {
    if (client->in_state == CLIENT_REQ_COMPLETE) {
      printf("client_process_in_stream: CLIENT_REQ_COMPLETE!\n");
      if (transaction->req_is_static)
        client->in_state = CLIENT_READING_HEADERS;
      break;
    }
    if (client->in_state == CLIENT_READING_HEADERS) {
      printf("client_process_in_stream: CLIENT_READING_HEADERS...\n");
      ssize_t hdr_end = find_double_crlf(buf->data, buf->len, 0);
      if (hdr_end == -1) break;
      if (http_req_is_body_complete(&client->in_stream)) {
        client->in_state = CLIENT_REQ_COMPLETE;
        transaction->req_len = (size_t)hdr_end;
      }
      if (http_req_is_connection_close(&client->in_stream, transaction->req_len))
        transaction->req_is_connection_close = true;
      bytes_read += transaction->req_len;
      if (client->in_state != CLIENT_REQ_COMPLETE) continue;
      transaction->req_is_http_one_point_o = http_is_one_point_o(
        client->in_stream.data, transaction->req_len
      );
      transaction->req_is_static = http_is_static_url(
        client, transaction->req_len
      );
      if (transaction->req_is_static) {
        http_build_static_resp(client, transaction->req_len);
        buffer_consume(&client->in_stream, transaction->req_len);
        client_epoll_toggle_write(epfd, client, 1);
      } else if (!client_borrow_backend(epfd, client)) {
        http_build_err_resp(
          client, BAD_GATEWAY_HEADER, BAD_GATEWAY_BODY, false
        );
        buffer_consume(&client->in_stream, transaction->req_len);
        client_epoll_toggle_write(epfd, client, 1);
      }
    } //TODO: CLIENT_READING_BODY
  }
  return 0;
}

int client_handle_read(int epfd, client_t *client) {
  printf("client_handle_read: reached!\n");
  buffer_t *buf = &client->in_stream;
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
      if (!client->backend && client->out_state == CLIENT_RESP_COMPLETE) {
        client_mark_closing(client);
        return -1;
      }
      return 0;
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

void client_reset_out_streams(client_t *client) {
  client->out_stream.len = 0;
  client->out_sent = 0;
  if (client->out_file_fd != -1) {
    close(client->out_file_fd);
    client->out_file_fd = -1;
  }
  client->out_file_offset = 0;
  client->out_file_size = 0;
  client->out_body_kind = BODY_BUFFER;
}

void client_reset_transaction(client_t *client) {
  transaction_t *transaction = &client->transaction;
  transaction_init(transaction);
}

int client_handle_write(int epfd, client_t *client) {
  printf("client_handle_write: %p!\n", client);
  if (client->closing) return -1;
  transaction_t *transaction = &client->transaction;
  for (;;) {
    int rc;
    if (client->out_state == CLIENT_RESP_COMPLETE) {
      printf("client_handle_write: client->out_state == CLIENT_RESP_COMPLETE\n");
      client->out_state = CLIENT_WRITING_RESP;
      client->in_state = CLIENT_READING_HEADERS;
      if (transaction->req_is_connection_close || transaction->req_is_http_one_point_o)
        goto close;
      client_reset_transaction(client);
      printf("client_handle_write: client->in_stream.len = %ld\n", client->in_stream.len);
      if (!client->backend && client->in_stream.len > 0) {
        assert(client->backend == NULL);
        assert(client->out_stream.len == 0);
        return client_process_in_stream(epfd, client);
      }
      client_epoll_toggle_write(epfd, client, 0);
      return 0;
    }
    if (client->out_state == CLIENT_WRITING_RESP) {
      printf("client_handle_write: client->out_state == CLIENT_WRITING_RESP\n");
      printf("client_handle_write: client->out_stream.len = %ld\n", client->out_stream.len);
      if (client->out_stream.len <= 0)
        return 0;
      rc = 1;
      if (client->out_body_kind == BODY_FILE) {
        printf("client_handle_write: client->out_file_fd = %d\n", client->out_file_fd);
        rc = buffer_send_flat(
          client->fd,
          &client->out_stream,
          client->out_stream.len,
          &client->out_sent
        );
        if (rc < 0) goto fail;
        if (rc == 0) return 0;
        rc = buffer_send_file(
          client->fd,
          client->out_file_fd,
          &client->out_file_offset,
          client->out_file_size
        );
      } else {
        printf("client_handle_write: "); print_client_out_stream(client);
        rc = buffer_send_flat(
          client->fd,
          &client->out_stream,
          client->out_stream.len,
          &client->out_sent
        );
      }
      if (rc < 0) goto fail;
      if (rc == 0) return 0;
      printf("client_handle_write: transaction->resp_header_complete = %d\n", transaction->resp_header_complete);
      if (transaction->req_is_static || transaction->resp_header_complete)
        client_reset_out_streams(client);
      if (
        !client->backend ||
        (transaction->req_is_static && client->out_body_kind == BODY_FILE)
      )
        client->out_state = CLIENT_RESP_COMPLETE;
      if (transaction->req_is_http_one_point_o) goto fail;
      continue;
    }
    return 0;
  }
close:
  client_mark_closing(client);
  return -1;
fail:
  client_mark_closing(client);
  return -1;
}

