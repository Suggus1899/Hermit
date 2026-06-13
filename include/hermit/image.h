#ifndef HERMIT_IMAGE_H
#define HERMIT_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define HERMIT_IMAGE_MANIFEST_VERSION "1.0"
#define HERMIT_IMAGE_CONFIG_VERSION "1.0"
#define HERMIT_MAX_LAYERS 128
#define HERMIT_MAX_DIGEST_LEN 128
#define HERMIT_MAX_IMAGE_NAME 256
#define HERMIT_MAX_TAG_LEN 64

struct hermit_image_layer {
    char digest[HERMIT_MAX_DIGEST_LEN];
    char media_type[64];
    size_t size;
    char path[PATH_MAX];
};

struct hermit_image_config {
    char architecture[32];
    char os[32];
    char *env_vars[32];
    int env_count;
    char *cmd[16];
    int cmd_count;
    char *entrypoint[16];
    int entrypoint_count;
    char working_dir[PATH_MAX];
    char user[64];
    int stop_signal;
    char *labels[16];
    int label_count;
};

struct hermit_image_manifest {
    char schema_version[16];
    char name[HERMIT_MAX_IMAGE_NAME];
    char tag[HERMIT_MAX_TAG_LEN];
    char created[64];
    struct hermit_image_config config;
    struct hermit_image_layer layers[HERMIT_MAX_LAYERS];
    int layer_count;
    size_t total_size;
};

int hermit_image_validate_name(const char *name);
int hermit_image_validate_tag(const char *tag);
int hermit_image_parse_manifest(const char *json_path, struct hermit_image_manifest *manifest);
int hermit_image_parse_manifest_from_string(const char *json_str, struct hermit_image_manifest *manifest);
int hermit_image_write_manifest(const char *json_path, const struct hermit_image_manifest *manifest);
int hermit_image_calculate_digest(const char *data, size_t len, char *digest_out);
int hermit_image_get_layer_path(const char *image_name, const char *digest, char *path_out, size_t path_size);

#endif
