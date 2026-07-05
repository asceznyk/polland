#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <time.h>

typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;

extern log_level_t g_log_level;

void log_write(log_level_t level, const char *file, int line, const char *fmt, ...);

#define LOG_DEBUG(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif
