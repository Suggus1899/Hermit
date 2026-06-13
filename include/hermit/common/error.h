#ifndef HERMIT_COMMON_ERROR_H
#define HERMIT_COMMON_ERROR_H

void hermit_die_errno(const char *context);
void hermit_die_msg(const char *fmt, ...);

#endif
