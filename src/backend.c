#include "backend.h"
#include "client.h"

void backend_init(struct backend_state *backend) {
  backend->fd = -1;
  backend->state = BE_CONNECTING;
  backend->send_off = 0;
}

int backend_connect(struct backend_state *backend, const char *ip, uint16_t port) {
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
  backend->fd = fd;
  backend->state = BE_CONNECTING;
  printf("backend connected! = %d\n", backend->fd);
  return 0;
}

bool backend_epoll_register(int epfd, struct client_state *client) {
  struct backend_state *backend = &client->backend;
  if (backend->fd < 0) return false;
  client->backend_ctx.kind = FD_BACKEND;
  client->backend_ctx.client = client;
  client->backend_ctx.fd = backend->fd;
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

static void backend_epoll_switch_state(
  int epfd,
  struct client_state *client,
  bool want_read,
  bool want_write
) {
  struct backend_state *backend = &client->backend;
  struct epoll_event evt = {0};
  uint32_t events = EPOLLHUP | EPOLLERR | EPOLLET;
  if (want_read) events |= EPOLLIN;
  if (want_write) events |= EPOLLOUT;
  evt.events = events;
  evt.data.ptr = &client->backend_ctx;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, backend->fd, &evt) < 0) {
    perror("epoll_ctl backend MOD");
    client_close_and_free(epfd, client);
  }
}

/*static void backend_handle_write(int epfd, struct client_state *client) {

}*/

void backend_close(int epfd, struct backend_state *backend) {
  //epoll_ctl(epfd, EPOLL_CTL_DEL, backend->fd, NULL);
  close(backend->fd);
  printf("backend closed!\n");
}


