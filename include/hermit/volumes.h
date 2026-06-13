#ifndef HERMIT_VOLUMES_H
#define HERMIT_VOLUMES_H

#include <stddef.h>
#include <stdbool.h>

#define HERMIT_MAX_VOLUMES 256
#define HERMIT_MAX_VOLUME_NAME 256
#define HERMIT_MAX_VOLUME_PATH 4096

struct hermit_volume {
    char name[HERMIT_MAX_VOLUME_NAME];
    char path[HERMIT_MAX_VOLUME_PATH];
    int ref_count;
    time_t created_at;
    size_t size;
    char driver[64];
};

struct hermit_volume_manager {
    char base_path[HERMIT_MAX_VOLUME_PATH];
    struct hermit_volume volumes[HERMIT_MAX_VOLUMES];
    int volume_count;
};

int hermit_volume_manager_init(struct hermit_volume_manager *mgr, const char *base_path);
int hermit_volume_create(struct hermit_volume_manager *mgr, const char *name, const char *driver);
int hermit_volume_remove(struct hermit_volume_manager *mgr, const char *name);
int hermit_volume_get(struct hermit_volume_manager *mgr, const char *name, struct hermit_volume **volume);
int hermit_volume_ref(struct hermit_volume_manager *mgr, const char *name);
int hermit_volume_unref(struct hermit_volume_manager *mgr, const char *name);
int hermit_volume_list(struct hermit_volume_manager *mgr, struct hermit_volume **volumes, int *count);
int hermit_volume_cleanup(struct hermit_volume_manager *mgr);
int hermit_volume_bind_mount(const char *source, const char *target, int readonly);

#endif
