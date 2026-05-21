#include "backend.h"
#include "utils.h"
#include "http.h"
#include "client.h"
#include "config.h"
#include "buffer.h"

void backend_init(struct backend_state *backend) {
  backend->client = NULL;
  backend->in_state = BE_CONNECTING;
  backend->out_state = BE_READING_HEADERS;
  backend->in_has_body = false;
  backend->in_sent = 0;
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
    fprintf(stderr, "backend_connect: suspicious fd=%d (stdio?), aborting\n", fd);
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
  printf("backend_connect: fd=%d\n", fd);
  return fd;
}

static void backend_attach_client(
  struct client_state *client,
  struct backend_state *backend
) {
  printf("backend_attach_client: attaching!\n");
  client->backend = backend;
  backend->client = client;
}

bool backend_epoll_register(int epfd, int fd, struct client_state *client) {
  printf("backend_epoll_register: reached!\n");
  struct backend_state *backend = calloc(1, sizeof(*backend));
  if (!backend || fd < 0) {
    close(fd);
    return false;
  }
  backend->fd = fd;
  backend_init(backend);
  backend->ctx.closing = false;
  backend->ctx.kind = FD_BACKEND;
  backend->ctx.peer = backend;
  printf("backend_epoll_register: backend_fd = %d\n", backend->fd);
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
  backend_attach_client(client, backend);
  return true;
}

void backend_epoll_toggle_write(
  int epfd,
  struct backend_state *backend,
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

void backend_detach_client(
  struct client_state *client,
  struct backend_state *backend
) {
  printf("backend_detach_client: detached!\n");
  client->backend = NULL;
  backend->client = NULL;
}

void backend_destroy(int epfd, struct backend_state *backend) {
  if (backend->fd == -1) return;
  printf("backend_destroy: closing backend with backend_fd = %d\n", backend->fd);
  epoll_ctl(epfd, EPOLL_CTL_DEL, backend->fd, NULL);
  close(backend->fd);
  backend->fd = -1;
  free(backend);
}

int backend_handle_err(int epfd, struct backend_state *backend) {
  struct client_state *client = backend->client;
  if (backend->in_state == BE_CONNECTING) {
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(backend->fd, SOL_SOCKET, SO_ERROR, &err, &len);
    printf("backend_handle_err: backend socket error: %s\n", strerror(err));
    http_build_err_resp(
      client, BAD_GATEWAY_HEADER, BAD_GATEWAY_BODY, false
    );
    buffer_consume(&client->in_stream, client->in_stream.len);
    client->out_state = CLIENT_WRITING_HEADERS;
    backend_detach_client(client, backend);
    backend_destroy(epfd, backend);
    client_epoll_toggle_write(epfd, client, 1);
  }
  return -1;
}

void backend_adv_out_state(struct backend_state *backend) {
  if (backend->out_state == BE_READING_HEADERS) {
    backend->out_state = BE_READING_BODY;
  } else if (backend->out_state == BE_READING_BODY) {
    backend->out_state = BE_RESP_COMPLETE;
  } else if (backend->out_state == BE_RESP_COMPLETE) {
    backend->out_state = BE_READING_HEADERS;
  }
}

int backend_handle_read(int epfd, struct backend_state *backend) {
  printf("backend_handle_read: reached!\n");
  struct client_state *client = backend->client;
  if (client->closing) return -1;
  int fd = backend->fd;
  struct buffer *dst = &client->out_stream;
  for (;;) {
    ssize_t n = recv(
      fd,
      dst->data + dst->len,
      dst->cap - dst->len,
      0
    );
    if (backend->out_state == BE_RESP_COMPLETE) backend_adv_out_state(backend);
    if (n > 0) {
      dst->len += n;
      if (
        (backend->out_state == BE_READING_HEADERS &&
        find_double_crlf(dst->data, dst->len, 0) != -1) ||
        backend->out_state == BE_READING_BODY
      ) backend_adv_out_state(backend);
      continue;
    }
    if (n == 0) {
      printf("backend_handle_read: n == 0! Stopping.\n");
      backend_detach_client(client, backend);
      backend_destroy(epfd, backend);
      client_epoll_toggle_write(epfd, client, 1);
      return 0;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      printf("backend_handle_read: EAGAIN | EWOULDBLOCK\n");
      break;
    };
    client_mark_closing(client);
    return -1;
  }
  if (client->out_stream.len > 0) client_epoll_toggle_write(epfd, client, 1);
  return 0;
}

void backend_adv_in_state(struct backend_state *backend) {
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

int backend_handle_write(int epfd, struct backend_state *backend) {
  printf("backend_handle_write: reached!\n");
  struct client_state *client = backend->client;
  if (client->closing) {
    printf("backend_handle_write: client->fd = %d closing...\n", client->fd);
    return -1;
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
        &backend->in_sent
      );
      if (rc < 0) goto fail;
      if (rc == 0) return 0;  // EAGAIN
      backend_adv_in_state(backend);
      buffer_consume(&client->in_stream, backend->in_sent);
      backend->in_sent = 0;
      continue;
    }
    client_mark_closing(client);
    return -1;
  }
fail:
  client_mark_closing(client);
  return -1;
}

