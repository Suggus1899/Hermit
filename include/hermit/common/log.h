#ifndef HERMIT_COMMON_LOG_H
#define HERMIT_COMMON_LOG_H

enum hermit_log_level {
    HERMIT_LOG_DEBUG = 0,
    HERMIT_LOG_INFO = 1,
    HERMIT_LOG_WARN = 2,
    HERMIT_LOG_ERROR = 3,
};

void hermit_log_set_level(enum hermit_log_level level);
void hermit_log(enum hermit_log_level level, const char *module, const char *fmt, ...);

#endif
