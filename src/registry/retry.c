#define _GNU_SOURCE
#include "hermit/registry_retry.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int calculate_delay_ms(const struct hermit_retry_config *config, int attempt)
{
    int delay = config->base_delay_ms;
    
    if (config->exponential_backoff && attempt > 0) {
        delay = config->base_delay_ms * (1 << attempt); // 2^attempt
    }
    
    // Apply jitter
    if (config->jitter_factor > 0) {
        double jitter = ((double)rand() / RAND_MAX) * config->jitter_factor;
        delay = (int)(delay * (1 + jitter));
    }
    
    // Cap at max delay
    if (delay > config->max_delay_ms) {
        delay = config->max_delay_ms;
    }
    
    return delay;
}

static int sleep_ms(int milliseconds)
{
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    return nanosleep(&ts, NULL);
}

int hermit_registry_push_with_retry(struct hermit_registry_client *client, const char *image_ref, 
                                const char *image_path, const struct hermit_retry_config *config)
{
    int attempt = 0;
    int last_error = 0;
    
    if (!client || !image_ref || !image_path || !config) {
        return -1;
    }
    
    struct hermit_retry_config default_config = {
        .max_retries = HERMIT_MAX_RETRIES,
        .base_delay_ms = HERMIT_RETRY_BASE_DELAY_MS,
        .max_delay_ms = HERMIT_RETRY_MAX_DELAY_MS,
        .exponential_backoff = true,
        .jitter_factor = 0.1
    };
    
    if (!config) {
        config = &default_config;
    }
    
    while (attempt <= config->max_retries) {
        hermit_log(HERMIT_LOG_INFO, "registry", "push attempt %d/%d for %s", 
                   attempt + 1, config->max_retries + 1, image_ref);
        
        int result = hermit_registry_push_image(client, image_ref, image_path);
        
        if (result == 0) {
            hermit_log(HERMIT_LOG_INFO, "registry", "push successful on attempt %d", attempt + 1);
            return 0;
        }
        
        last_error = result;
        
        if (attempt < config->max_retries) {
            int delay_ms = calculate_delay_ms(config, attempt);
            hermit_log(HERMIT_LOG_WARN, "registry", "push failed, retrying in %d ms", delay_ms);
            sleep_ms(delay_ms);
        }
        
        attempt++;
    }
    
    hermit_log(HERMIT_LOG_ERROR, "registry", "push failed after %d attempts", attempt);
    return last_error;
}

int hermit_registry_pull_with_retry(struct hermit_registry_client *client, const char *image_ref, 
                                const char *output_path, const struct hermit_retry_config *config)
{
    int attempt = 0;
    int last_error = 0;
    
    if (!client || !image_ref || !output_path || !config) {
        return -1;
    }
    
    struct hermit_retry_config default_config = {
        .max_retries = HERMIT_MAX_RETRIES,
        .base_delay_ms = HERMIT_RETRY_BASE_DELAY_MS,
        .max_delay_ms = HERMIT_RETRY_MAX_DELAY_MS,
        .exponential_backoff = true,
        .jitter_factor = 0.1
    };
    
    if (!config) {
        config = &default_config;
    }
    
    while (attempt <= config->max_retries) {
        hermit_log(HERMIT_LOG_INFO, "registry", "pull attempt %d/%d for %s", 
                   attempt + 1, config->max_retries + 1, image_ref);
        
        int result = hermit_registry_pull_image(client, image_ref, output_path);
        
        if (result == 0) {
            hermit_log(HERMIT_LOG_INFO, "registry", "pull successful on attempt %d", attempt + 1);
            return 0;
        }
        
        last_error = result;
        
        if (attempt < config->max_retries) {
            int delay_ms = calculate_delay_ms(config, attempt);
            hermit_log(HERMIT_LOG_WARN, "registry", "pull failed, retrying in %d ms", delay_ms);
            sleep_ms(delay_ms);
        }
        
        attempt++;
    }
    
    hermit_log(HERMIT_LOG_ERROR, "registry", "pull failed after %d attempts", attempt);
    return last_error;
}

int hermit_registry_init_mirrors(struct hermit_mirror_config *config, const char *primary, 
                               const char **mirrors, int mirror_count)
{
    if (!config || !primary) {
        return -1;
    }
    
    memset(config, 0, sizeof(*config));
    
    strncpy(config->primary_registry, primary, sizeof(config->primary_registry) - 1);
    
    if (mirrors && mirror_count > 0) {
        config->mirror_count = mirror_count < 8 ? mirror_count : 8;
        for (int i = 0; i < config->mirror_count; i++) {
            strncpy(config->mirrors[i], mirrors[i], sizeof(config->mirrors[i]) - 1);
        }
    }
    
    config->current_mirror_index = 0;
    config->enable_fallback = true;
    
    hermit_log(HERMIT_LOG_INFO, "registry", "initialized mirrors: primary=%s, mirrors=%d", 
               primary, config->mirror_count);
    
    return 0;
}

static int try_mirror_operation(struct hermit_mirror_config *mirror_config,
                            int (*operation)(struct hermit_registry_client *, const char *, const char *),
                            struct hermit_registry_client *client, const char *image_ref, const char *path)
{
    char registry_url[HERMIT_MAX_REGISTRY_URL];
    const char *target_registry;
    
    // Determine which registry to use
    if (mirror_config->current_mirror_index == 0) {
        target_registry = mirror_config->primary_registry;
    } else if (mirror_config->current_mirror_index <= mirror_config->mirror_count) {
        target_registry = mirror_config->mirrors[mirror_config->current_mirror_index - 1];
    } else {
        return -1; // No more mirrors to try
    }
    
    // Initialize client with target registry
    strncpy(registry_url, target_registry, sizeof(registry_url) - 1);
    
    if (hermit_registry_client_init(client, registry_url) != 0) {
        return -1;
    }
    
    // Attempt the operation
    int result = operation(client, image_ref, path);
    
    if (result == 0) {
        hermit_log(HERMIT_LOG_INFO, "registry", "operation successful with registry: %s", target_registry);
        return 0;
    }
    
    hermit_log(HERMIT_LOG_WARN, "registry", "operation failed with registry: %s", target_registry);
    return -1;
}

int hermit_registry_operation_with_mirrors(struct hermit_mirror_config *mirror_config,
                                         int (*operation)(struct hermit_registry_client *, const char *, const char *),
                                         struct hermit_registry_client *client, const char *image_ref, const char *path)
{
    int attempt = 0;
    int max_attempts = mirror_config->mirror_count + 2; // primary + mirrors
    
    if (!mirror_config || !operation || !client || !image_ref) {
        return -1;
    }
    
    while (attempt < max_attempts) {
        hermit_log(HERMIT_LOG_INFO, "registry", "trying mirror %d/%d: %s", 
                   attempt + 1, max_attempts, 
                   attempt == 0 ? mirror_config->primary_registry : mirror_config->mirrors[attempt - 1]);
        
        int result = try_mirror_operation(mirror_config, operation, client, image_ref, path);
        
        if (result == 0) {
            // Success - reset to primary for next operation
            mirror_config->current_mirror_index = 0;
            return 0;
        }
        
        // Move to next mirror
        mirror_config->current_mirror_index++;
        attempt++;
        
        // Small delay between mirror attempts
        if (attempt < max_attempts) {
            sleep_ms(1000);
        }
    }
    
    hermit_log(HERMIT_LOG_ERROR, "registry", "operation failed with all mirrors");
    mirror_config->current_mirror_index = 0; // Reset for next operation
    return -1;
}
