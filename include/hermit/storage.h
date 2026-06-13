#ifndef HERMIT_STORAGE_H
#define HERMIT_STORAGE_H

#include <stddef.h>
#include <stdbool.h>

#define HERMIT_MAX_LAYERS 128
#define HERMIT_MAX_PATH 4096

enum hermit_storage_driver_type {
    HERMIT_STORAGE_OVERLAYFS,
    HERMIT_STORAGE_DIRECT,
    HERMIT_STORAGE_COPY
};

struct hermit_storage_layer {
    char digest[HERMIT_MAX_DIGEST_LEN];
    char path[HERMIT_MAX_PATH];
    bool is_readonly;
    size_t size;
};

struct hermit_storage_driver {
    char name[64];
    char base_path[HERMIT_MAX_PATH];
    enum hermit_storage_driver_type type;
    struct hermit_storage_layer layers[HERMIT_MAX_LAYERS];
    int layer_count;
    char current_mount[HERMIT_MAX_PATH];
    bool mounted;
};

int hermit_storage_init(struct hermit_storage_driver *driver, const char *base_path);
int hermit_storage_add_layer(struct hermit_storage_driver *driver, const char *digest, const char *path, bool readonly);
int hermit_storage_create_overlay(struct hermit_storage_driver *driver, const char *mount_path);
int hermit_storage_mount(struct hermit_storage_driver *driver, const char *mount_path);
int hermit_storage_unmount(struct hermit_storage_driver *driver);
int hermit_storage_cleanup(struct hermit_storage_driver *driver);
int hermit_storage_commit_changes(struct hermit_storage_driver *driver, const char *new_digest);
int hermit_storage_detect_driver_type(enum hermit_storage_driver_type *type);
int hermit_storage_copy_up(const char *source, const char *target);
int hermit_storage_rootless_mount(struct hermit_storage_driver *driver, const char *mount_path);
int hermit_storage_gc_orphaned_layers(const char *layers_dir, const char *images_dir);
int hermit_storage_record_layer_usage(const char *layer_digest, const char *image_name);
int hermit_storage_get_layer_usage_count(const char *layer_digest);

#endif
