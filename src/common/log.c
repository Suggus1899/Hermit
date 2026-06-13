#include "hermit/common/log.h"

#include <stdarg.h>
#include <stdio.h>

static enum hermit_log_level g_level = HERMIT_LOG_INFO;

static const char *level_name(enum hermit_log_level level)
{
    switch (level) {
    case HERMIT_LOG_DEBUG:
        return "DEBUG";
    case HERMIT_LOG_INFO:
        return "INFO";
    case HERMIT_LOG_WARN:
        return "WARN";
    case HERMIT_LOG_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

void hermit_log_set_level(enum hermit_log_level level)
{
    g_level = level;
}

void hermit_log(enum hermit_log_level level, const char *module, const char *fmt, ...)
{
    va_list ap;

    if (level < g_level) {
        return;
    }

    fprintf(stderr, "[hermit][%s][%s] ", level_name(level), module);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}
