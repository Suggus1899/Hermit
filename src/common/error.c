#include "hermit/common/error.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hermit_die_errno(const char *context)
{
    int saved_errno = errno;
    fprintf(stderr, "[hermit][error] %s failed: errno=%d (%s)\n",
            context, saved_errno, strerror(saved_errno));
    exit(EXIT_FAILURE);
}

void hermit_die_msg(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "[hermit][error] ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    exit(EXIT_FAILURE);
}
