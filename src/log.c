#include "log.h"
#include <string.h>
#include <unistd.h>
#include <time.h>

static log_level_t g_log_level = LOG_LEVEL_INFO;

static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

void log_init(log_level_t level)
{
    g_log_level = level;
}

log_level_t log_get_level(void)
{
    return g_log_level;
}

void log_msg(log_level_t level, const char *tag, const char *fmt, ...)
{
    if (level < g_log_level)
        return;

    /* Timestamp for production log traceability */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    fprintf(stderr, "%04d-%02d-%02d %02d:%02d:%02d [%s] [%s] ",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
            level_names[level], tag);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    /* Add newline if the format string doesn't end with one */
    size_t len = strlen(fmt);
    if (len == 0 || fmt[len - 1] != '\n')
        fputc('\n', stderr);
}

void log_signal_safe(const char *msg)
{
    /* write(2) is async-signal-safe, unlike fprintf */
    if (msg) {
        size_t len = 0;
        const char *p = msg;
        while (*p++) len++;
        (void)write(STDERR_FILENO, msg, len);
    }
}
