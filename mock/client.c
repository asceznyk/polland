#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_CHUNK_DELAY_US 200000
#define DEFAULT_NUM_CLIENTS 100
#define DEFAULT_EXPECTED_RESPONSES 3
#define DEFAULT_URL "http://127.0.0.1:6969/"
#define BUFFER_BYTES_PER_RESPONSE 8192
#define MAX_URLS 64

enum client_mode {
  MODE_BURST,
  MODE_DELAYED,
};

struct endpoint {
  char host[256];
  char port[16];
  char path[1024];
  char host_header[272];
};

struct config {
  enum client_mode mode;
  int chunk_delay_us;
  int num_clients;
  int expected_responses;
  int url_count;
  struct endpoint urls[MAX_URLS];
};

struct thread_arg {
  int id;
  const struct config *config;
};

static void usage(const char *prog) {
  fprintf(
    stderr,
    "Usage: %s [--mode burst|delayed] [--chunk-delay-us N] [--num-clients N] [--expected-responses N] [--url URL]...\n",
    prog
  );
}

static int parse_positive_int(const char *name, const char *value) {
  char *end = NULL;
  long parsed = strtol(value, &end, 10);
  if (value[0] == '\0' || *end != '\0' || parsed <= 0 || parsed > INT32_MAX) {
    fprintf(stderr, "invalid %s: %s\n", name, value);
    exit(EXIT_FAILURE);
  }
  return (int)parsed;
}

static enum client_mode parse_mode(const char *value) {
  if (strcmp(value, "burst") == 0)
    return MODE_BURST;
  if (strcmp(value, "delayed") == 0)
    return MODE_DELAYED;
  fprintf(stderr, "invalid mode: %s\n", value);
  exit(EXIT_FAILURE);
}

static void parse_url(const char *url, struct endpoint *endpoint) {
  const char *prefix = "http://";
  const size_t prefix_len = strlen(prefix);
  if (strncmp(url, prefix, prefix_len) != 0) {
    fprintf(stderr, "unsupported URL scheme in %s\n", url);
    exit(EXIT_FAILURE);
  }
  const char *host_start = url + prefix_len;
  const char *path_start = strchr(host_start, '/');
  const char *host_end = path_start != NULL ? path_start : url + strlen(url);
  const char *colon = NULL;
  for (const char *p = host_start; p < host_end; ++p) {
    if (*p == ':')
      colon = p;
  }
  size_t host_len = (size_t)((colon != NULL ? colon : host_end) - host_start);
  if (host_len == 0 || host_len >= sizeof(endpoint->host)) {
    fprintf(stderr, "invalid host in %s\n", url);
    exit(EXIT_FAILURE);
  }
  memcpy(endpoint->host, host_start, host_len);
  endpoint->host[host_len] = '\0';
  if (colon != NULL) {
    size_t port_len = (size_t)(host_end - colon - 1);
    if (port_len == 0 || port_len >= sizeof(endpoint->port)) {
      fprintf(stderr, "invalid port in %s\n", url);
      exit(EXIT_FAILURE);
    }
    memcpy(endpoint->port, colon + 1, port_len);
    endpoint->port[port_len] = '\0';
  } else {
    strcpy(endpoint->port, "80");
  }
  if (path_start != NULL) {
    size_t path_len = strlen(path_start);
    if (path_len >= sizeof(endpoint->path)) {
      fprintf(stderr, "path too long in %s\n", url);
      exit(EXIT_FAILURE);
    }
    memcpy(endpoint->path, path_start, path_len + 1);
  } else {
    strcpy(endpoint->path, "/");
  }
  if (snprintf(
        endpoint->host_header,
        sizeof(endpoint->host_header),
        "%s:%s",
        endpoint->host,
        endpoint->port
      ) >= (int)sizeof(endpoint->host_header)) {
    fprintf(stderr, "host header too long for %s\n", url);
    exit(EXIT_FAILURE);
  }
}

static void validate_same_origin(const struct endpoint *lhs, const struct endpoint *rhs) {
  if (strcmp(lhs->host, rhs->host) != 0 || strcmp(lhs->port, rhs->port) != 0) {
    fprintf(stderr, "all --url values must use the same host and port\n");
    exit(EXIT_FAILURE);
  }
}

static int requests_per_batch(const struct config *config) {
  return config->url_count;
}

static int total_expected_responses(const struct config *config) {
  return config->expected_responses * requests_per_batch(config);
}

static void parse_args(int argc, char **argv, struct config *config) {
  int saw_url = 0;
  config->mode = MODE_BURST;
  config->chunk_delay_us = DEFAULT_CHUNK_DELAY_US;
  config->num_clients = DEFAULT_NUM_CLIENTS;
  config->expected_responses = DEFAULT_EXPECTED_RESPONSES;
  config->url_count = 1;
  parse_url(DEFAULT_URL, &config->urls[0]);
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--mode") == 0) {
      if (++i >= argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
      }
      config->mode = parse_mode(argv[i]);
      continue;
    }
    if (strcmp(argv[i], "--chunk-delay-us") == 0) {
      if (++i >= argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
      }
      config->chunk_delay_us = parse_positive_int("chunk-delay-us", argv[i]);
      continue;
    }
    if (strcmp(argv[i], "--num-clients") == 0) {
      if (++i >= argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
      }
      config->num_clients = parse_positive_int("num-clients", argv[i]);
      continue;
    }
    if (strcmp(argv[i], "--expected-responses") == 0) {
      if (++i >= argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
      }
      config->expected_responses = parse_positive_int("expected-responses", argv[i]);
      continue;
    }
    if (strcmp(argv[i], "--url") == 0) {
      if (++i >= argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
      }
      if (!saw_url) {
        parse_url(argv[i], &config->urls[0]);
        saw_url = 1;
        continue;
      }
      if (config->url_count >= MAX_URLS) {
        fprintf(stderr, "too many --url values (max %d)\n", MAX_URLS);
        exit(EXIT_FAILURE);
      }
      parse_url(argv[i], &config->urls[config->url_count]);
      config->url_count++;
      saw_url = 1;
      continue;
    }
    if (strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      exit(EXIT_SUCCESS);
    }
    usage(argv[0]);
    fprintf(stderr, "unknown argument: %s\n", argv[i]);
    exit(EXIT_FAILURE);
  }

  for (int i = 1; i < config->url_count; ++i)
    validate_same_origin(&config->urls[0], &config->urls[i]);
}

static int count_http_responses(const char *buf) {
  int count = 0;
  const char *p = buf;
  while ((p = strstr(p, "HTTP/1.1")) != NULL) {
    count++;
    p += 8;
  }
  return count;
}

static int connect_to_endpoint(const struct endpoint *endpoint) {
  struct addrinfo hints = {0};
  struct addrinfo *result = NULL;
  int sock = -1;

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  int rc = getaddrinfo(endpoint->host, endpoint->port, &hints, &result);
  if (rc != 0) {
    fprintf(stderr, "getaddrinfo(%s:%s): %s\n", endpoint->host, endpoint->port, gai_strerror(rc));
    return -1;
  }

  for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock < 0)
      continue;
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
      break;
    close(sock);
    sock = -1;
  }

  freeaddrinfo(result);
  return sock;
}

static int send_all(int sock, const char *buf, size_t len) {
  size_t total_sent = 0;
  while (total_sent < len) {
    ssize_t n = send(sock, buf + total_sent, len - total_sent, 0);
    if (n <= 0) {
      perror("send");
      return -1;
    }
    total_sent += (size_t)n;
  }
  return 0;
}

static int send_request_for_endpoint(
  int sock,
  const struct endpoint *endpoint,
  const char *connection,
  useconds_t delay_us,
  enum client_mode mode
) {
  if (mode == MODE_BURST) {
    char request[1792];
    int written = snprintf(
      request,
      sizeof(request),
      "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: %s\r\n\r\n",
      endpoint->path,
      endpoint->host_header,
      connection
    );
    if (written < 0 || written >= (int)sizeof(request)) {
      fprintf(stderr, "request buffer too small\n");
      return -1;
    }
    return send_all(sock, request, (size_t)written);
  }

  char request_line[1200];
  int written = snprintf(request_line, sizeof(request_line), "GET %s HTTP/1.1\r\n", endpoint->path);
  if (written < 0 || written >= (int)sizeof(request_line)) {
    fprintf(stderr, "request path too long\n");
    return -1;
  }
  if (send_all(sock, request_line, (size_t)written) < 0)
    return -1;
  usleep(delay_us);

  char host_line[384];
  written = snprintf(host_line, sizeof(host_line), "Host: %s\r\n", endpoint->host_header);
  if (written < 0 || written >= (int)sizeof(host_line)) {
    fprintf(stderr, "host header too long\n");
    return -1;
  }
  if (send_all(sock, host_line, (size_t)written) < 0)
    return -1;
  usleep(delay_us);

  char connection_line[64];
  written = snprintf(connection_line, sizeof(connection_line), "Connection: %s\r\n", connection);
  if (written < 0 || written >= (int)sizeof(connection_line)) {
    fprintf(stderr, "connection header error\n");
    return -1;
  }
  if (send_all(sock, connection_line, (size_t)written) < 0)
    return -1;
  usleep(delay_us);

  if (send_all(sock, "User-Agent: mock\r\n\r\n", strlen("User-Agent: mock\r\n\r\n")) < 0)
    return -1;
  usleep(delay_us);
  return 0;
}

static int send_requests(int sock, const struct config *config) {
  for (int i = 0; i < config->expected_responses; ++i) {
    int is_last_batch = i + 1 == config->expected_responses;
    for (int j = 0; j < config->url_count; ++j) {
      int is_last_request = is_last_batch && (j + 1 == config->url_count);
      const char *connection = is_last_request ? "close" : "keep-alive";
      if (send_request_for_endpoint(
            sock,
            &config->urls[j],
            connection,
            (useconds_t)config->chunk_delay_us,
            config->mode
          ) < 0) {
        return -1;
      }
    }
  }
  return 0;
}

static void *client_thread(void *arg) {
  const struct thread_arg *thread = arg;
  const struct config *config = thread->config;
  int id = thread->id;
  int sock = connect_to_endpoint(&config->urls[0]);
  if (sock < 0) {
    perror("connect");
    return NULL;
  }
  printf("[client %d] connected\n", id);
  int send_rc = send_requests(sock, config);
  if (send_rc < 0) {
    close(sock);
    return NULL;
  }
  size_t buffer_size = (size_t)total_expected_responses(config) * BUFFER_BYTES_PER_RESPONSE;
  char *buffer = malloc(buffer_size);
  if (buffer == NULL) {
    perror("malloc");
    close(sock);
    return NULL;
  }
  ssize_t total = 0;
  ssize_t n;
  int responses = 0;
  while ((n = recv(sock, buffer + total, buffer_size - (size_t)total - 1, 0)) > 0) {
    total += n;
    buffer[total] = '\0';
    responses = count_http_responses(buffer);
    if (responses >= total_expected_responses(config))
      break;
    if ((size_t)total >= buffer_size - 1) {
      fprintf(stderr, "[client %d] buffer overflow risk\n", id);
      break;
    }
  }
  if (n < 0)
    perror("recv");
  buffer[total] = '\0';
  printf("\n==============================\n");
  printf("[client %d] FULL RESPONSE (%ld bytes):\n", id, total);
  printf("%s\n", buffer);
  printf("==============================\n");
  printf("[client %d] received %d responses (expected %d)\n",
         id,
         responses,
         total_expected_responses(config));
  if (responses != total_expected_responses(config)) {
    fprintf(stderr,
            "[client %d] ERROR: expected %d responses but got %d\n",
            id,
            total_expected_responses(config),
            responses);
    free(buffer);
    close(sock);
    abort();
  }
  free(buffer);
  close(sock);
  printf("[client %d] done\n", id);
  return NULL;
}

int main(int argc, char **argv) {
  struct config config;
  parse_args(argc, argv, &config);
  pthread_t *threads = calloc((size_t)config.num_clients, sizeof(*threads));
  struct thread_arg *thread_args = calloc((size_t)config.num_clients, sizeof(*thread_args));
  if (threads == NULL || thread_args == NULL) {
    perror("calloc");
    free(threads);
    free(thread_args);
    return EXIT_FAILURE;
  }
  for (int i = 0; i < config.num_clients; i++) {
    thread_args[i].id = i;
    thread_args[i].config = &config;
    pthread_create(&threads[i], NULL, client_thread, &thread_args[i]);
  }
  for (int i = 0; i < config.num_clients; i++)
    pthread_join(threads[i], NULL);
  free(threads);
  free(thread_args);
  printf("All clients completed successfully.\n");
  return 0;
}
