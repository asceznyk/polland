#include "log.h"
#include "backend.h"
#include "utils.h"
#include "http.h"
#include "client.h"
#include "config.h"
#include "buffer.h"

int backend_entry_count = 0;

backend_t *backend_registry[MAX_UPSTREAM_CONNECTIONS] = {0};

void backend_show_registry() {
  for (int i = 0; i < MAX_UPSTREAM_CONNECTIONS; i++) {
    backend_t *b = backend_registry[i];
    if (b == NULL) continue;
    LOG_DEBUG("backend_show_registry: %p", b);
  }
}

void backend_mark_closing(backend_t *backend) {
  LOG_DEBUG("backend_mark_closing: backend = %p", backend);
  if (backend->ctx.closing || backend->closing) return;
  backend->ctx.closing = true;
  backend->closing = true;
}

void backend_init(backend_t *backend, int fd) {
  backend->fd = fd;
  backend->closing = false;
  backend->client = NULL;
  backend->pool_state = BE_IDLE;
  backend->in_state = BE_CONNECTING;
  backend->out_state = BE_READING_HEADERS;
  backend->in_has_body = false;
  backend->in_sent = 0;
  backend->next = NULL;
  backend->prev = NULL;
  backend->ctx.closing = false;
  backend->ctx.kind = FD_BACKEND;
  backend->ctx.peer = backend;
  backend->ctx.fd = fd;
}

void backend_reset_states(backend_t *backend) {
  backend->in_sent = 0;
  backend->in_state = BE_WRITING_REQ;
  backend->out_state = BE_READING_HEADERS;
}

int backend_connect(const char *ip, uint16_t port) {
  int fd = -1;
  int flags;
  struct sockaddr_in addr;
  int rc;
  if (!ip) return -1;
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }
  if (fd <= 2) {
    fprintf(stderr, "backend_connect: suspicious fd = %d (stdio?), aborting", fd);
    close(fd);
    return -1;
  }
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    perror("fcntl(F_GETFL)");
    close(fd);
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    perror("fcntl(F_SETFL)");
    close(fd);
    return -1;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    fprintf(stderr, "inet_pton failed for %s", ip);
    close(fd);
    return -1;
  }
  rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (rc < 0) {
    if (errno != EINPROGRESS) {
      perror("connect");
      close(fd);
      return -1;
    }
  }
  LOG_DEBUG("backend_connect: fd = %d", fd);
  return fd;
}

backend_t *backend_create(int fd) {
  LOG_DEBUG("backend_create: creating with fd = %d", fd);
  backend_t *backend = malloc(sizeof(*backend));
  if (!backend || fd < 0) {
    close(fd);
    return NULL;
  }
  backend->ridx = backend_entry_count;
  backend_registry[backend_entry_count++] = backend;
  backend_init(backend, fd);
  return backend;
}

backend_t *backend_build_pool(int epfd, int k) {
  assert(k >= 0);
  backend_t *start = NULL;
  backend_t *prev = NULL;
  while (k--) {
    int fd = -1;
    if((
      fd = backend_connect(server_cfg.upstream.host, server_cfg.upstream.port)
    ) < 0) return NULL;
    backend_t *curr = backend_create(fd);
    if (!curr) return start;
    if (!backend_epoll_register(epfd, curr)) break;
    backend_epoll_toggle_write(epfd, curr, 0);
    curr->prev = prev;
    curr->next = NULL;
    if (prev) prev->next = curr;
    else start = curr;
    prev = curr;
  }
  return start;
}

void backend_remove_from_pool(backend_t *backend) {
  LOG_DEBUG("backend_remove_from_pool: backend = %p, backend_pool = %p", backend, backend_pool);
  backend_t **addr = &backend_pool;
  if (backend->prev) backend->prev->next = backend->next;
  else *addr = backend->next;
  if (backend->next) backend->next->prev = backend->prev;
  LOG_DEBUG("backend_remove_from_pool: backend_pool = %p", backend_pool);
}

backend_t *backend_detach_from_pool() {
  if (!backend_pool) return NULL;
  backend_t *backend = backend_pool;
  backend_pool = backend->next;
  if (backend_pool) backend_pool->prev = NULL;
  backend->next = NULL;
  backend->prev = NULL;
  backend->pool_state = BE_IN_USE;
  return backend;
}

void backend_attach_to_pool(backend_t *backend) {
  LOG_DEBUG("backend_attach_to_pool: backend = %p, backend_pool = %p!", backend, backend_pool);
  assert(backend->next == NULL);
  assert(backend->prev == NULL);
  backend->prev = NULL;
  backend->next = backend_pool;
  backend->pool_state = BE_IDLE;
  if (backend_pool) backend_pool->prev = backend;
  backend_pool = backend;
}

void backend_attach_client(
  client_t *client,
  backend_t *backend
) {
  LOG_DEBUG("backend_attach_client: attaching!");
  assert(backend->client == NULL);
  assert(client->backend == NULL);
  client->backend = backend;
  backend->client = client;
}

void backend_detach_client(
  client_t *client,
  backend_t *backend
) {
  LOG_DEBUG("backend_detach_client: detached!");
  assert(backend->client == client);
  assert(client->backend == backend);
  client->backend = NULL;
  backend->client = NULL;
}

bool backend_epoll_register(int epfd, backend_t *backend) {
  LOG_DEBUG("backend_epoll_register: backend = %p", backend);
  int flags = fcntl(backend->fd, F_GETFL, 0);
  if (flags < 0 || fcntl(backend->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(backend->fd);
    return false;
  }
  struct epoll_event evt = {0};
  evt.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  evt.data.ptr = &backend->ctx;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, backend->fd, &evt) < 0) {
    perror("epoll_ctl backend ADD");
    close(backend->fd);
    return false;
  }
  return true;
}

void backend_epoll_toggle_write(
  int epfd,
  backend_t *backend,
  bool add_write
) {
  LOG_DEBUG("backend_epoll_toggle_write: add_write = %d", add_write);
  struct epoll_event evt = {0};
  uint32_t events = EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  if (add_write) events |= EPOLLOUT;
  evt.events = events;
  evt.data.ptr = &backend->ctx;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, backend->fd, &evt) < 0) {
    perror("epoll_ctl backend MOD");
  }
}

void backend_destroy(int epfd, backend_t *backend) {
  if (backend->fd == -1) return;
  LOG_DEBUG("backend_destroy: destroying backend %p!", backend);
  epoll_ctl(epfd, EPOLL_CTL_DEL, backend->fd, NULL);
  close(backend->fd);
  backend->fd = -1;
  backend_registry[backend->ridx] = backend_registry[backend_entry_count-1];
  backend_registry[backend->ridx]->ridx = backend->ridx;
  backend_registry[backend_entry_count-1] = NULL;
  backend_entry_count--;
  free(backend);
}

void backend_prepare_closing(backend_t *backend) {
  LOG_DEBUG("backend_prepare_closing: %p!", backend);
  if (backend->client) backend_detach_client(backend->client, backend);
  LOG_DEBUG(
    "backend_prepare_closing: backend->next = %p, backend->prev = %p, backend->pool_state = %d",
    backend->next, backend->prev, backend->pool_state
  );
  if (backend->pool_state == BE_IDLE) backend_remove_from_pool(backend);
  backend_mark_closing(backend);
}

void backend_err_update_client(client_t *client, backend_t *backend) {
  LOG_DEBUG("backend_err_update_client: client = %p, backend = %p", client, backend);
  assert(client != NULL);
  transaction_t *transaction = &client->transaction;
  http_build_err_resp(
    client, BAD_GATEWAY_HEADER, BAD_GATEWAY_BODY, false
  );
  if (backend->in_state != BE_REQ_COMPLETE)
    buffer_consume(&client->in_stream, client->transaction.req_len);
  client->out_state = CLIENT_WRITING_RESP;
  transaction->resp_header_complete = true;
}

int backend_handle_err(int epfd, backend_t *backend) {
  LOG_DEBUG("backend_handle_err: %p", backend);
  client_t *client = backend->client;
  if (!client) return -1;
  backend_prepare_closing(backend);
  int err = 0;
  socklen_t len = sizeof(err);
  getsockopt(backend->fd, SOL_SOCKET, SO_ERROR, &err, &len);
  LOG_DEBUG("backend_handle_err: backend error upstream: (%s)", strerror(err));
  if (backend->out_state == BE_RESP_COMPLETE) return -1;
  backend_err_update_client(client, backend);
  client_epoll_toggle_write(epfd, client, 1);
  return -1;
}

static int backend_handle_idle(backend_t *backend) {
  LOG_DEBUG("backend_handle_idle: %p", backend);
  char buf[1024];
  for (;;) {
    ssize_t n = recv(backend->fd, buf, sizeof(buf), 0);
    if (n == 0) {
      LOG_DEBUG("backend_handle_idle: backend closed connection!");
      backend_prepare_closing(backend);
      return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      LOG_DEBUG("backend_handle_idle: EAGAIN | EWOULDBLOCK");
      break;
    };
    backend_prepare_closing(backend);
    return -1;
  }
  return 0;
}

int backend_return_client(backend_t *backend) {
  LOG_DEBUG("backend_return_client: %p", backend);
  if (!backend || backend->closing) {
    if (backend->closing) {
      LOG_DEBUG("backend_return_client: backend->closing!");
      return -1;
    }
    return 0;
  }
  backend_detach_client(backend->client, backend);
  backend_reset_states(backend);
  backend_attach_to_pool(backend);
  return 0;
}

int backend_handle_read(int epfd, backend_t *backend) {
  LOG_DEBUG("backend_handle_read: %p", backend);
  LOG_DEBUG("backend_handle_read: client = %p", backend->client);
  if (backend->closing) {
    if (backend->client) backend_detach_client(backend->client, backend);
    return -1;
  };
  client_t *client = backend->client;
  if (!client) return backend_handle_idle(backend);
  if (client->closing) {
    LOG_DEBUG("backend_handle_read: closing client %p", client);
    return 0;
  }
  int fd = backend->fd;
  buffer_t *buf = &client->out_stream;
  transaction_t *transaction = &client->transaction;
  for (;;) {
    ssize_t n = recv(
      fd,
      buf->data + buf->len,
      buf->cap - buf->len,
      0
    );
    if (n > 0) {
      buf->len += n;
      transaction->resp_len += n;
      if (backend->out_state == BE_READING_HEADERS) {
        LOG_DEBUG("backend_handle_read: BE_READING_HEADERS");
        transaction->resp_header_len = find_double_crlf(buf->data, buf->len, 0);
        bool is_hdr_end = (transaction->resp_header_len > 0);
        transaction->resp_header_complete = is_hdr_end;
        transaction->resp_header_content_len = is_hdr_end
          ? http_get_content_length(buf)
          : 0;
        LOG_DEBUG("backend_handle_read: transaction->resp_len = %zu", transaction->resp_len);
        http_resp_redact_server_name(buf, transaction);
        LOG_DEBUG("backend_handle_read: transaction->resp_len = %zu", transaction->resp_len);
        backend->out_state = is_hdr_end ? BE_READING_BODY : BE_READING_HEADERS;
      }
      if (backend->out_state == BE_READING_BODY) {
        LOG_DEBUG("backend_handle_read: BE_READING_BODY");
        assert(transaction->resp_header_len > -1);
        transaction->resp_body_len = transaction->resp_len - transaction->resp_header_len;
        LOG_DEBUG("backend_handle_read: transaction->resp_header_len = %zu", transaction->resp_header_len);
        LOG_DEBUG(
          "backend_handle_read: transaction->resp_body_len = %zu, transaction->resp_header_content_len = %zu",
          transaction->resp_body_len,
          transaction->resp_header_content_len
        );
        backend->out_state = (transaction->resp_body_len >= transaction->resp_header_content_len)
          ? BE_RESP_COMPLETE
          : BE_READING_BODY;
      }
      continue;
    }
    if (n == 0) {
      LOG_DEBUG("backend_handle_read: n == 0! backend closed connection!");
      if (backend->out_state == BE_RESP_COMPLETE)
        backend_prepare_closing(backend);
      else {
        backend_detach_client(client, backend);
        backend_mark_closing(backend);
        backend_err_update_client(client, backend);
        client_epoll_toggle_write(epfd, client, 1);
        return -1;
      }
      break;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      LOG_DEBUG("backend_handle_read: EAGAIN | EWOULDBLOCK");
      LOG_DEBUG("backend_handle_read: "); print_client_out_stream(client);
      LOG_DEBUG("backend_handle_read: backend->out_state = %d", backend->out_state);
      break;
    };
    LOG_DEBUG("backend_handle_read: errno = %d (%s)", errno, strerror(errno));
    backend_mark_closing(backend);
    return -1;
  }
  if (client->out_stream.len > 0) client_epoll_toggle_write(epfd, client, 1);
  int status = 0;
  if (backend->out_state == BE_RESP_COMPLETE)
    status = backend_return_client(backend);
  return status;
}

int backend_handle_write(int epfd, backend_t *backend) {
  LOG_DEBUG("backend_handle_write: %p", backend);
  if (backend->closing) {
    if (backend->client) backend_detach_client(backend->client, backend);
    return -1;
  };
  client_t *client = backend->client;
  if (!client) return 0;
  LOG_DEBUG("backend_handle_write: "); print_client_in_stream(client);
  if (client->closing) return 0;
  for (;;) {
    int rc;
    if (backend->in_state == BE_CONNECTING) {
      LOG_DEBUG("backend_handle_write: BE_CONNECTING");
      int err = 0;
      socklen_t len = sizeof(err);
      if (getsockopt(backend->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        LOG_DEBUG("backend_handle_write: failed %s!", strerror(err));
        goto fail;
      }
      if (err != 0) goto fail;
      backend->in_state = BE_WRITING_REQ;
      continue;
    }
    if (backend->in_state == BE_REQ_COMPLETE) {
      LOG_DEBUG("backend_handle_write: BE_REQ_COMPLETE");
      backend_epoll_toggle_write(epfd, backend, 0);
      return 0;
    }
    if (backend->in_state == BE_WRITING_REQ) {
      LOG_DEBUG("backend_handle_write: BE_WRITING_REQ");
      rc = buffer_send_flat(
        backend->fd,
        &client->in_stream,
        client->transaction.req_len,
        &backend->in_sent
      );
      if (rc < 0) goto fail;
      if (rc == 0) return 0; // EAGAIN
      if (client->in_state == CLIENT_REQ_COMPLETE)
        backend->in_state = BE_REQ_COMPLETE;
      buffer_consume(&client->in_stream, backend->in_sent);
      backend->in_sent = 0;
      continue;
    }
    return -1;
  }
fail:
  return -1;
}

