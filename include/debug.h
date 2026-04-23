#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG_FD

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void dump_bt(void) {
  void *buf[32];
  int n = backtrace(buf, 32);
  backtrace_symbols_fd(buf, n, STDERR_FILENO);
}

static inline int debug_close(int fd) {
  if (fd <= 2) {
    fprintf(stderr, "\n*** close(%d) detected ***\n", fd);
    dump_bt();
    abort();
  }
  return close(fd); // calls real close (macro not yet applied here)
}

#define close(fd) debug_close(fd)

#endif // DEBUG_FD
#endif // DEBUG_H

