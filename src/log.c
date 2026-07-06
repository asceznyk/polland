#include "log.h"
#include <stdarg.h>

log_level_t g_log_level = LVL_INFO;

static const char *level_str(log_level_t l) {
  switch (l) {
    case LVL_DEBUG: return "DEBUG";
    case LVL_INFO: return "INFO";
    case LVL_WARN: return "WARN";
    default: return "ERROR";
  }
}

void log_write(
  log_level_t level,
  const char *file,
  int line,
  const char *fmt,
  ...
) {
  if (level < g_log_level) return;
  char msg[1024];
  time_t t = time(NULL);
  char timebuf[20];
  strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&t));
  int pos = snprintf(
    msg, sizeof(msg),
    "[%s] %s %s:%d: ", timebuf,
    level_str(level), file, line
  );
  va_list args;
  va_start(args, fmt);
  pos += vsnprintf(msg + pos, sizeof(msg) - pos, fmt, args);
  va_end(args);
  snprintf(msg + pos, sizeof(msg) - pos, "\n");
  fputs(msg, stderr);
}

