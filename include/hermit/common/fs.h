#ifndef HERMIT_COMMON_FS_H
#define HERMIT_COMMON_FS_H

#include <limits.h>
#include <sys/types.h>

#define HERMIT_PATH_CONTAINERS "containers"
#define HERMIT_PATH_IMAGES "images"
#define HERMIT_PATH_VOLUMES "volumes"
#define HERMIT_PATH_REGISTRY "registry"
#define HERMIT_PATH_ORCHESTRATOR "orchestrator"

typedef struct hermit_app_context {
    char data_root[PATH_MAX];
} hermit_app_context;

int hermit_path_join2(char *dst, size_t dst_len, const char *a, const char *b);
int hermit_sanitize_image_ref(const char *ref, char *out, size_t out_len);
int hermit_ensure_dir(const char *path, mode_t mode);
int hermit_init_data_root(hermit_app_context *ctx);
int hermit_list_dir_entries(const char *path);

#endif
