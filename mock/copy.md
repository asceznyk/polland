So I have this script to check load with concurrent clients

```
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>

#define CHUNK_DELAY_US 200000
#define NUM_CLIENTS 80
#define BUFFER_SIZE (8192 * NUM_CLIENTS)
#define EXPECTED_RESPONSES 3

void send_chunk(int sock, const char *chunk) {
  ssize_t total = 0;
  ssize_t len = strlen(chunk);
  while (total < len) {
    ssize_t n = send(sock, chunk + total, len - total, 0);
    if (n <= 0) {
      perror("send");
      return;
    }
    total += n;
  }
  usleep(CHUNK_DELAY_US);
}

int count_http_responses(const char *buf) {
  int count = 0;
  const char *p = buf;
  while ((p = strstr(p, "HTTP/1.1 ")) != NULL) {
    count++;
    p += 9;
  }
  return count;
}

void *client_thread(void *arg) {
  int id = (intptr_t)arg;
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return NULL;
  }
  struct sockaddr_in server_addr = {
    .sin_family = AF_INET,
    .sin_port = htons(6969),
    .sin_addr.s_addr = inet_addr("127.0.0.1")
  };
  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    close(sock);
    return NULL;
  }
  printf("[client %d] connected\n", id);
  for (int i = 0; i < 3; i++) {
    send_chunk(sock, "GET / HTTP/1.1\r\n");
    send_chunk(sock, "Host: localhost:6969\r\n");
    if (i < 2)
      send_chunk(sock, "Connection: keep-alive\r\n");
    else
      send_chunk(sock, "Connection: keep-alive\r\n");
    send_chunk(sock, "User-Agent: mock\r\n\r\n");
  }
  char buffer[BUFFER_SIZE];
  ssize_t total = 0;
  ssize_t n;
  int sresp = 0;
  while ((n = recv(sock, buffer + total, sizeof(buffer) - total - 1, 0)) > 0) {
    total += n;
    sresp = count_http_responses(buffer);
    if (sresp >= EXPECTED_RESPONSES) break;
    if (total >= BUFFER_SIZE - 1) {
      fprintf(stderr, "[client %d] buffer overflow risk\n", id);
      break;
    }
  }
  if (n < 0)
    perror("recv");
  buffer[total] = 0;
  int responses = count_http_responses(buffer);
  printf("\n==============================\n");
  printf("[client %d] FULL RESPONSE (%ld bytes):\n", id, total);
  printf("%s\n", buffer);
  printf("==============================\n");
  printf("[client %d] received %d responses (expected %d)\n", id, responses, EXPECTED_RESPONSES);
  if (responses != EXPECTED_RESPONSES) {
    fprintf(
      stderr,
      "[client %d] ERROR: expected %d responses but got %d\n",
      id, EXPECTED_RESPONSES, responses
    );
    abort();
  }
  close(sock);
  printf("[client %d] done\n", id);
  return NULL;
}

int main() {
  pthread_t threads[NUM_CLIENTS];
  for (int i = 0; i < NUM_CLIENTS; i++)
    pthread_create(&threads[i], NULL, client_thread, (void *)(intptr_t)i);
  for (int i = 0; i < NUM_CLIENTS; i++)
    pthread_join(threads[i], NULL);
  printf("All clients completed successfully.\n");
  return 0;
}
```

It works fine with `50-60` clients, but on `80-100` clients it throws `send: Connection reset by peer`

What is the reason?


