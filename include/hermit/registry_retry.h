#ifndef HERMIT_REGISTRY_RETRY_H
#define HERMIT_REGISTRY_RETRY_H

#include "hermit/registry.h"
#include <stddef.h>
#include <stdbool.h>

#define HERMIT_MAX_RETRIES 5
#define HERMIT_RETRY_BASE_DELAY_MS 1000
#define HERMIT_RETRY_MAX_DELAY_MS 30000

struct hermit_retry_config {
    int max_retries;
    int base_delay_ms;
    int max_delay_ms;
    bool exponential_backoff;
    double jitter_factor;
};

struct hermit_mirror_config {
    char primary_registry[HERMIT_MAX_REGISTRY_URL];
    char mirrors[8][HERMIT_MAX_REGISTRY_URL];
    int mirror_count;
    int current_mirror_index;
    bool enable_fallback;
};

int hermit_registry_push_with_retry(struct hermit_registry_client *client, const char *image_ref, 
                                const char *image_path, const struct hermit_retry_config *config);
int hermit_registry_pull_with_retry(struct hermit_registry_client *client, const char *image_ref, 
                                const char *output_path, const struct hermit_retry_config *config);
int hermit_registry_init_mirrors(struct hermit_mirror_config *config, const char *primary, 
                               const char **mirrors, int mirror_count);
int hermit_registry_operation_with_mirrors(struct hermit_mirror_config *mirror_config,
                                         int (*operation)(struct hermit_registry_client *, const char *, const char *),
                                         struct hermit_registry_client *client, const char *image_ref, const char *path);

#endif
