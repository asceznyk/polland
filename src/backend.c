#include "backend.h"
#include "utils.h"
#include "http.h"
#include "client.h"
#include "buffer.h"

void backend_init(struct backend_state *backend) {
  backend->fd = -1;
  backend->state = BE_CONNECTING;
  backend->in_headers_sent = 0;
  backend->in_body_sent = 0;
}

int backend_connect(
  struct backend_state *backend, const char *ip, uint16_t port
) {
  int fd;
  int flags;
  struct sockaddr_in addr;
  int rc;
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(fd);
    return -1;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    close(fd);
    return -1;
  }
  rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (rc < 0 && errno != EINPROGRESS) {
    close(fd);
    return -1;
  }
  printf("backend_connect: rc = %d!\n", rc);
  backend->fd = fd;
  backend->state = BE_CONNECTING;
  return 0;
}

bool backend_epoll_register(int epfd, struct client_state *client) {
  printf("backend_epoll_register: reached!\n");
  struct backend_state *backend = &client->backend;
  if (backend->fd < 0) return false;
  client->backend_ctx.kind = FD_BACKEND;
  client->backend_ctx.client = client;
  client->backend_ctx.fd = backend->fd;
  printf("backend_epoll_register: backend_fd = %d\n", backend->fd);
  int flags = fcntl(backend->fd, F_GETFL, 0);
  if (flags < 0 || fcntl(backend->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(backend->fd);
    return false;
  }
  struct epoll_event evt = {0};
  evt.events = EPOLLOUT | EPOLLET | EPOLLHUP | EPOLLERR;
  evt.data.ptr = &client->backend_ctx;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, backend->fd, &evt) < 0) {
    perror("epoll_ctl backend ADD");
    close(backend->fd);
    return false;
  }
  return true;
}

void backend_close(int epfd, struct backend_state *backend) {
  epoll_ctl(epfd, EPOLL_CTL_DEL, backend->fd, NULL);
  close(backend->fd);
  backend->fd = -1;
  printf("backend_close: closed with backend_fd = %d!\n", backend->fd);
}

static void backend_epoll_switch_state(
  int epfd,
  struct client_state *client,
  bool want_read,
  bool want_write
) {
  struct backend_state *backend = &client->backend;
  printf("backend_epoll_switch_state, want_read = %d, want_write = %d, backend_fd = %d\n", want_read, want_write, backend->fd);
  struct epoll_event evt = {0};
  uint32_t events = EPOLLHUP | EPOLLERR | EPOLLET;
  if (want_read) {
    printf("backend_epoll_switch_state: EPOLLIN\n");
    events |= EPOLLIN;
  }
  if (want_write) events |= EPOLLOUT;
  evt.events = events;
  evt.data.ptr = &client->backend_ctx;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, backend->fd, &evt) < 0) {
    perror("epoll_ctl backend MOD");
    backend_close(epfd, &client->backend);
  }
}

void backend_http_adv_state(struct backend_state *backend) {
  if (backend->state == BE_CONNECTING) {
    backend->state = BE_WRITING_HEADERS;
  } else if (backend->state == BE_WRITING_HEADERS) {
    backend->state = BE_WRITING_BODY;
  } else if (backend->state == BE_WRITING_BODY) {
    backend->state = BE_READING_HEADERS;
  } else if (backend->state == BE_READING_HEADERS) {
    backend->state = BE_READING_BODY;
  } else if (backend->state == BE_READING_BODY) {
    backend->state = BE_DONE;
  }
}

void backend_handle_err(int epfd, struct client_state *client) {
  struct backend_state *backend = &client->backend;
  if (backend->state == BE_CONNECTING) {
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(backend->fd, SOL_SOCKET, SO_ERROR, &err, &len);
    printf("Backend socket error: %s\n", strerror(err));
    backend_close(epfd, backend);
    http_build_err_resp(
      client, BAD_GATEWAY_HEADER, BAD_GATEWAY_BODY, false
    );
    buffer_consume(&client->in_headers, client->in_header_end);
    buffer_consume(&client->in_body, client->in_body_end);
    client->in_header_end = 0;
    client->in_body_end = 0;
    client->state = CLIENT_WRITING_HEADERS;
    client_epoll_switch_state(epfd, client, 1);
    return;
  }
  backend_close(epfd, backend);
  client_close_and_free(epfd, client);
}

void backend_handle_read(int epfd, struct client_state *client) {
  printf("backend_handle_read: reached!\n");
  struct backend_state *backend = &client->backend;
  int fd = backend->fd;
  for (;;) {
    struct buffer *dst;
    if (backend->state == BE_READING_HEADERS) dst = &client->out_headers;
    else if (backend->state == BE_READING_BODY) dst = &client->out_body;
    else return;
    ssize_t n = recv(
      fd,
      dst->data + dst->len,
      dst->cap - dst->len,
      0
    );
    if (n > 0) {
      dst->len += n;
      if (
        backend->state == BE_READING_HEADERS &&
        find_double_crlf(dst->data, dst->len, 0)
      ) {
        backend_http_adv_state(backend); // -> BE_READING_BODY
      }
      continue;
    }
    if (n == 0) {
      backend_http_adv_state(backend);
      backend_epoll_switch_state(epfd, client, 0, 0);
      backend_close(epfd, backend);
      client_epoll_switch_state(epfd, client, 1);
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      printf("errno = %d (%s)\n", errno, strerror(errno));
      break;
    };
    backend_close(epfd, backend);
    client_close_and_free(epfd, client);
    return;
  }
  /*if (client->out_headers.len > 0 || client->out_body.len > 0) {
    client_epoll_switch_state(epfd, client, 1);
  }*/
}

void backend_handle_write(int epfd, struct client_state *client) {
  printf("backend_handle_write: reached!\n");
  struct backend_state *backend = &client->backend;
  for (;;) {
    int rc;
    if (backend->state == BE_CONNECTING) {
      printf("backend_handle_write: BE_CONNECTING\n");
      int err = 0;
      socklen_t len = sizeof(err);
      if (getsockopt(backend->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        printf("backend_handle_write: FAILED TO CONNECT!\n");
        goto fail;
      }
      if (err != 0) goto fail;
      backend_http_adv_state(backend);
      continue;
    }
    if (backend->state == BE_WRITING_HEADERS) {
      rc = buffer_send_flat(
        backend->fd,
        &client->in_headers,
        &backend->in_headers_sent
      );
      if (rc < 0) goto fail;
      if (rc == 0) return;  // EAGAIN
      backend_http_adv_state(backend);
      buffer_consume(&client->in_headers, client->in_header_end);
      backend->in_headers_sent = 0;
      continue;
    }
    if (backend->state == BE_WRITING_BODY) {
      rc = buffer_send_flat(
        backend->fd,
        &client->in_body,
        &backend->in_body_sent
      );
      if (rc < 0) goto fail;
      if (rc == 0) return;  // EAGAIN
      backend_http_adv_state(backend);
      buffer_consume(&client->in_body, client->in_body_end);
      backend->in_body_sent = 0;
      backend_epoll_switch_state(epfd, client, 1, 0);
      return;
    }
    return;
  }
fail:
  backend_close(epfd, backend);
}


