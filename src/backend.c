#include "backend.h"
#include "utils.h"
#include "http.h"
#include "client.h"
#include "config.h"
#include "buffer.h"

int backend_entry_count = 0;

backend_t *backend_registry[MAX_UPSTREAM_CONNECTIONS] = {0};

void backend_show_registry() {
  printf("backend_show_registry: backend_registry = ");
  for (int i = 0; i < backend_entry_count; i++) {
    backend_t *item = backend_registry[i];
    printf("%d, ", item->fd);
  }
  printf("\n");
}

void backend_mark_closing(backend_t *backend) {
  printf("backend_mark_closing: backend->fd = %d\n", backend->fd);
  if (backend->ctx.closing || backend->closing) return;
  backend->ctx.closing = true;
  backend->closing = true;
}

void backend_init(backend_t *backend, int fd) {
  backend->fd = fd;
  backend->closing = false;
  backend->client = NULL;
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
    fprintf(stderr, "backend_connect: suspicious fd = %d (stdio?), aborting\n", fd);
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
    fprintf(stderr, "inet_pton failed for %s\n", ip);
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
  printf("backend_connect: fd = %d\n", fd);
  return fd;
}

backend_t *backend_create(int fd) {
  printf("backend_create: creating with fd = %d\n", fd);
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
  printf("backend_remove_from_pool: backend->fd = %d, backend_pool = %p\n", backend->fd, backend_pool);
  backend_t **addr = &backend_pool;
  if (backend->prev) backend->prev->next = backend->next;
  else *addr = backend->next;
  if (backend->next) backend->next->prev = backend->prev;
  printf("backend_remove_from_pool: backend_pool = %p\n", backend_pool);
}

backend_t *backend_detach_from_pool() {
  printf("backend_detach_from_pool: %p\n", backend_pool);
  backend_t *backend = NULL;
  if (!backend_pool) return NULL;
  if (!backend_pool->next) {
    printf("backend_detach_from_pool: next is NULL?!\n");
    backend = backend_pool;
    backend_pool = NULL;
    return backend;
  };
  backend = backend_pool;
  backend_pool = backend_pool->next;
  backend_pool->prev = NULL;
  backend->next = NULL;
  return backend;
}

void backend_attach_to_pool(backend_t *backend) {
  printf("backend_attach_to_pool: backend_pool = %p!\n", backend_pool);
  if (!backend_pool) {
    backend_pool = backend;
    return;
  }
  backend->next = backend_pool;
  backend->next->prev = backend;
  backend_pool = backend;
}

void backend_attach_client(
  client_t *client,
  backend_t *backend
) {
  printf("backend_attach_client: attaching!\n");
  client->backend = backend;
  backend->client = client;
}

void backend_detach_client(
  client_t *client,
  backend_t *backend
) {
  printf("backend_detach_client: detached!\n");
  client->backend = NULL;
  backend->client = NULL;
}

bool backend_epoll_register(int epfd, backend_t *backend) {
  printf("backend_epoll_register: backend->fd = %d\n", backend->fd);
  int flags = fcntl(backend->fd, F_GETFL, 0);
  if (flags < 0 || fcntl(backend->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(backend->fd);
    return false;
  }
  struct epoll_event evt = {0};
  evt.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
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
  printf("backend_epoll_toggle_write: add_write = %d\n", add_write);
  struct epoll_event evt = {0};
  uint32_t events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
  if (add_write) events |= EPOLLOUT;
  evt.events = events;
  evt.data.ptr = &backend->ctx;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, backend->fd, &evt) < 0) {
    perror("epoll_ctl backend MOD");
  }
}

void backend_close(int epfd, backend_t *backend) {
  printf("backend_close: closing backend with fd = %d\n", backend->fd);
  epoll_ctl(epfd, EPOLL_CTL_DEL, backend->fd, NULL);
  close(backend->fd);
  backend->fd = -1;
}

void backend_destroy(int epfd, backend_t *backend) {
  if (backend->fd == -1) return;
  printf("backend_destroy: destroying backend with fd = %d\n", backend->fd);
  epoll_ctl(epfd, EPOLL_CTL_DEL, backend->fd, NULL);
  close(backend->fd);
  backend->fd = -1;
  backend_registry[backend->ridx] = backend_registry[backend_entry_count-1];
  backend_registry[backend->ridx]->ridx = backend->ridx;
  backend_entry_count--;
  printf("backend_destroy: destroying %p!\n", backend);
  free(backend);
}

int backend_handle_err(int epfd, backend_t *backend) {
  client_t *client = backend->client;
  if (backend->in_state == BE_CONNECTING) {
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(backend->fd, SOL_SOCKET, SO_ERROR, &err, &len);
    printf("backend_handle_err: backend socket error: %s\n", strerror(err));
    http_build_err_resp(
      client, BAD_GATEWAY_HEADER, BAD_GATEWAY_BODY, false
    );
    buffer_consume(&client->in_stream, client->req_len);
    client->out_state = CLIENT_WRITING_HEADERS;
    backend_detach_client(client, backend);
    backend_destroy(epfd, backend);
    client_epoll_toggle_write(epfd, client, 1);
  }
  return -1;
}

void backend_adv_out_state(backend_t *backend) {
  if (backend->out_state == BE_READING_HEADERS) {
    backend->out_state = BE_READING_BODY;
  } else if (backend->out_state == BE_READING_BODY) {
    backend->out_state = BE_RESP_COMPLETE;
  } else if (backend->out_state == BE_RESP_COMPLETE) {
    backend->out_state = BE_READING_HEADERS;
  }
}

static int backend_handle_idle(int epfd, backend_t *backend) {
  printf("backend_handle_idle: reached!\n");
  char buf[1024];
  for (;;) {
    ssize_t n = recv(backend->fd, buf, sizeof(buf), 0);
    if (n == 0) {
      printf("backend_handle_idle: backend closed connection!\n");
      backend_remove_from_pool(backend);
      backend_mark_closing(backend);
      return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      printf("backend_handle_idle: EAGAIN | EWOULDBLOCK\n");
      break;
    };
    backend_remove_from_pool(backend);
    backend_mark_closing(backend);
    return -1;
  }
  return 0;
}

void backend_return_client(backend_t *backend) {
  printf("backend_return_client: reached!\n");
  if (!backend || backend->closing) {
    if (backend->closing) printf("backend_return_client: backend->closing!\n");
    return;
  }
  backend_detach_client(backend->client, backend);
  backend_attach_to_pool(backend);
}

int backend_handle_read(int epfd, backend_t *backend) {
  printf("backend_handle_read: reached!\n");
  if (backend->closing) return -1;
  client_t *client = backend->client;
  if (!client) return backend_handle_idle(epfd, backend);
  printf("backend_handle_read: client->req_id = %d\n", client->req_id);
  if (client->closing) return 0;
  int fd = backend->fd;
  struct buffer *dst = &client->out_stream;
  for (;;) {
    ssize_t n = recv(
      fd,
      dst->data + dst->len,
      dst->cap - dst->len,
      0
    );
    if (n > 0) {
      dst->len += n;
      if (
        (backend->out_state == BE_READING_HEADERS &&
        find_double_crlf(dst->data, dst->len, 0) != -1) ||
        backend->out_state == BE_READING_BODY
      ) backend_adv_out_state(backend);
      if (http_is_resp_complete(&client->out_stream))
        backend->out_state = BE_RESP_COMPLETE;
      continue;
    }
    if (n == 0) {
      printf("backend_handle_read: n == 0! backend closed connection!\n");
      backend_detach_client(client, backend);
      backend_remove_from_pool(backend);
      backend_mark_closing(backend);
      break;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      printf("backend_handle_read: EAGAIN | EWOULDBLOCK\n");
      printf("backend_handle_read: backend->out_state = %d\n", backend->out_state);
      break;
    };
    return -1;
  }
  if (backend->out_state == BE_RESP_COMPLETE) backend_return_client(backend);
  if (client->out_stream.len > 0) client_epoll_toggle_write(epfd, client, 1);
  return 0;
}

void backend_adv_in_state(backend_t *backend) {
  if (backend->in_state == BE_CONNECTING) {
    backend->in_state = BE_WRITING_HEADERS;
  } else if (backend->in_state == BE_WRITING_HEADERS) {
    backend->in_state = backend->in_has_body ?
      BE_WRITING_BODY :
      BE_REQ_COMPLETE;
  } else if (backend->in_state == BE_WRITING_BODY) {
    backend->in_state = BE_REQ_COMPLETE;
  } else if (backend->in_state == BE_REQ_COMPLETE) {
    backend->in_state = BE_WRITING_HEADERS;
  }
}

int backend_handle_write(int epfd, backend_t *backend) {
  printf("backend_handle_write: reached!\n");
  if (backend->closing) return -1;
  client_t *client = backend->client;
  if (!client) return 0;
  printf("backend_handle_write: client->req_id = %d\n", client->req_id);
  printf("backend_handle_write: client->req_len = %ld\n", client->req_len);
  if (client->closing) {
    printf("backend_handle_write: client->fd = %d closing...\n", client->fd);
    return 0;
  }
  for (;;) {
    int rc;
    if (backend->in_state == BE_CONNECTING) {
      int err = 0;
      socklen_t len = sizeof(err);
      if (getsockopt(backend->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        goto fail;
      }
      if (err != 0) goto fail;
      backend_adv_in_state(backend);
      continue;
    }
    if (backend->in_state == BE_REQ_COMPLETE) {
      backend_adv_in_state(backend);
      backend_epoll_toggle_write(epfd, backend, 0);
      return 0;
    }
    if (
      backend->in_state == BE_WRITING_HEADERS ||
      backend->in_state == BE_WRITING_BODY
    ) {
      rc = buffer_send_flat(
        backend->fd,
        &client->in_stream,
        client->req_len,
        &backend->in_sent
      );
      if (rc < 0) goto fail;
      if (rc == 0) return 0;  // EAGAIN
      backend_adv_in_state(backend);
      buffer_consume(&client->in_stream, backend->in_sent);
      backend->in_sent = 0;
      continue;
    }
    //client_mark_closing(client);
    return -1;
  }
fail:
  //client_mark_closing(client);
  return -1;
}

