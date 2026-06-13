#ifndef HERMIT_DOCKERFILE_IGNORE_H
#define HERMIT_DOCKERFILE_IGNORE_H

#include <stddef.h>

#define HERMIT_MAX_PATTERNS 256
#define HERMIT_MAX_PATTERN_LEN 1024

struct hermit_ignore_patterns {
    char patterns[HERMIT_MAX_PATTERNS][HERMIT_MAX_PATTERN_LEN];
    int pattern_count;
};

int hermit_parse_dockerignore(const char *context_path, struct hermit_ignore_patterns *patterns);
int hermit_is_ignored(const struct hermit_ignore_patterns *patterns, const char *path);
void hermit_free_ignore_patterns(struct hermit_ignore_patterns *patterns);

#endif
