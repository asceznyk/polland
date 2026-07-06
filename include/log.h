#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <time.h>

typedef enum { LVL_DEBUG, LVL_INFO, LVL_WARN, LVL_ERROR } log_level_t;

extern log_level_t g_log_level;

void log_write(log_level_t level, const char *file, int line, const char *fmt, ...);

#define LOG_DEBUG(...) log_write(LVL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LVL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LVL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LVL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif
