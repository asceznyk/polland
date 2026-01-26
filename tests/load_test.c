#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define CHUNK_DELAY_US 200000
#define NUM_CLIENTS 20

void send_chunk(int sock, const char *chunk) {
  send(sock, chunk, strlen(chunk), 0);
  usleep(CHUNK_DELAY_US);
}

void* client_thread(void *arg) {
  int id = (intptr_t)arg;
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in server_addr = {
    .sin_family = AF_INET,
    .sin_port = htons(6969),
    .sin_addr.s_addr = inet_addr("127.0.0.1")
  };
  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    return NULL;
  }
  printf("[client %d] connected\n", id);
  send_chunk(sock, "GE");
  send_chunk(sock, "T / HT");
  send_chunk(sock, "TP/1.1\r\n");
  send_chunk(sock, "Host: local");
  send_chunk(sock, "host:6969\r\n");
  send_chunk(sock, "User-Agent: mc\r\n");
  send_chunk(sock, "\r\n");
  char buf[2048];
  int n = recv(sock, buf, sizeof(buf)-1, 0);
  if (n > 0) {
    buf[n] = 0;
    printf("[client %d] response:\n%s\n", id, buf);
  }
  close(sock);
  printf("[client %d] done\n", id);
  return NULL;
}

int main() {
  pthread_t threads[NUM_CLIENTS];
  for (int i = 0; i < NUM_CLIENTS; i++) {
    pthread_create(&threads[i], NULL, client_thread, (void *)(intptr_t)i);
  }
  for (int i = 0; i < NUM_CLIENTS; i++) {
    pthread_join(threads[i], NULL);
  }
  return 0;
}

