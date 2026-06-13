#ifndef HERMIT_BUILDER_H
#define HERMIT_BUILDER_H

#include "hermit/dockerfile.h"
#include "hermit/image.h"

#define HERMIT_MAX_BUILD_STEPS 128
#define HERMIT_BUILD_CONTEXT_DIR "/tmp/hermit_build_context"

struct hermit_build_step {
    enum hermit_instruction_type type;
    char *args[HERMIT_MAX_ARGS];
    int arg_count;
    char layer_digest[HERMIT_MAX_DIGEST_LEN];
    char cache_key[256];
    bool cached;
    bool completed;
};

struct hermit_build_context {
    char *image_name;
    char *tag;
    char *context_path;
    char *dockerfile_path;
    struct hermit_dockerfile dockerfile;
    struct hermit_build_step steps[HERMIT_MAX_BUILD_STEPS];
    int step_count;
    char current_layer_dir[PATH_MAX];
    struct hermit_image_manifest manifest;
};

int hermit_builder_init(struct hermit_build_context *ctx, const char *image_name, 
                     const char *tag, const char *context_path, const char *dockerfile_path);
int hermit_builder_execute(struct hermit_build_context *ctx);
int hermit_builder_cleanup(struct hermit_build_context *ctx);
int hermit_builder_create_layer(struct hermit_build_context *ctx, struct hermit_build_step *step);
int hermit_builder_apply_layer(struct hermit_build_context *ctx, const char *layer_path);
int hermit_builder_generate_cache_key(const struct hermit_build_step *step, const char *context_hash, char *cache_key);
int hermit_builder_check_cache(const char *cache_key, char *layer_digest);

#endif
