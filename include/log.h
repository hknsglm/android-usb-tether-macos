#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

/* Initialize logging with minimum level threshold */
void log_init(log_level_t level);

/* Get current log level */
log_level_t log_get_level(void);

/* Core logging function */
void log_msg(log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Signal-safe logging (uses write(2) instead of fprintf) */
void log_signal_safe(const char *msg);

/* Convenience macros */
#define LOG_D(tag, ...) log_msg(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)
#define LOG_I(tag, ...) log_msg(LOG_LEVEL_INFO,  tag, __VA_ARGS__)
#define LOG_W(tag, ...) log_msg(LOG_LEVEL_WARN,  tag, __VA_ARGS__)
#define LOG_E(tag, ...) log_msg(LOG_LEVEL_ERROR, tag, __VA_ARGS__)

#endif /* LOG_H */
