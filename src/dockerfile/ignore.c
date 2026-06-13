#define _GNU_SOURCE
#include "hermit/dockerfile_ignore.h"

#include "hermit/common/log.h"

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hermit_parse_dockerignore(const char *context_path, struct hermit_ignore_patterns *patterns)
{
    char ignore_path[PATH_MAX];
    FILE *fp;
    char line[HERMIT_MAX_PATTERN_LEN];
    
    if (!context_path || !patterns) {
        return -1;
    }
    
    memset(patterns, 0, sizeof(*patterns));
    
    snprintf(ignore_path, sizeof(ignore_path), "%s/.hermitignore", context_path);
    
    fp = fopen(ignore_path, "r");
    if (!fp) {
        // No .hermitignore file is OK
        return 0;
    }
    
    while (fgets(line, sizeof(line), fp) != NULL && patterns->pattern_count < HERMIT_MAX_PATTERNS) {
        // Remove newline and trim whitespace
        line[strcspn(line, "\r\n")] = '\0';
        
        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        // Store pattern
        strcpy(patterns->patterns[patterns->pattern_count], line);
        patterns->pattern_count++;
    }
    
    fclose(fp);
    
    hermit_log(HERMIT_LOG_INFO, "dockerfile", "loaded %d ignore patterns from .hermitignore", 
               patterns->pattern_count);
    
    return 0;
}

static int matches_pattern(const char *pattern, const char *path)
{
    // Simple glob matching
    return fnmatch(pattern, path, FNM_PATHNAME) == 0;
}

int hermit_is_ignored(const struct hermit_ignore_patterns *patterns, const char *path)
{
    if (!patterns || !path) {
        return 0;
    }
    
    for (int i = 0; i < patterns->pattern_count; i++) {
        if (matches_pattern(patterns->patterns[i], path)) {
            return 1;
        }
    }
    
    return 0;
}

void hermit_free_ignore_patterns(struct hermit_ignore_patterns *patterns)
{
    if (!patterns) return;
    memset(patterns, 0, sizeof(*patterns));
}
