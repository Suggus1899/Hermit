#define _GNU_SOURCE
#include "hermit/volumes.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int validate_path_safety(const char *base_path, const char *target_path)
{
    char resolved_base[HERMIT_MAX_VOLUME_PATH];
    char resolved_target[HERMIT_MAX_VOLUME_PATH];
    char resolved_check[HERMIT_MAX_VOLUME_PATH];
    
    if (realpath(base_path, resolved_base) == NULL) {
        return -1;
    }
    
    if (realpath(target_path, resolved_target) == NULL) {
        if (strstr(target_path, "..") != NULL) {
            hermit_log(HERMIT_LOG_ERROR, "volumes", "path traversal detected: %s", target_path);
            return -1;
        }
        return 0;
    }
    
    if (realpath(target_path, resolved_check) == NULL) {
        return 0;
    }
    
    size_t base_len = strlen(resolved_base);
    if (strncmp(resolved_target, resolved_base, base_len) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "target path escapes base: %s", resolved_target);
        return -1;
    }
    
    if (strstr(target_path, "..") != NULL) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "path traversal attempt: %s", target_path);
        return -1;
    }
    
    return 0;
}

static int save_volume_metadata(struct hermit_volume_manager *mgr)
{
    char metadata_path[HERMIT_MAX_VOLUME_PATH];
    FILE *fp;
    
    snprintf(metadata_path, sizeof(metadata_path), "%s/volumes.metadata", mgr->base_path);
    
    fp = fopen(metadata_path, "w");
    if (!fp) {
        return -1;
    }
    
    fprintf(fp, "# Hermit Volume Metadata\n");
    fprintf(fp, "count=%d\n", mgr->volume_count);
    fprintf(fp, "updated_at=%ld\n", time(NULL));
    
    for (int i = 0; i < mgr->volume_count; i++) {
        struct hermit_volume *vol = &mgr->volumes[i];
        fprintf(fp, "volume_%d_name=%s\n", i, vol->name);
        fprintf(fp, "volume_%d_path=%s\n", i, vol->path);
        fprintf(fp, "volume_%d_ref_count=%d\n", i, vol->ref_count);
        fprintf(fp, "volume_%d_created_at=%ld\n", i, vol->created_at);
        fprintf(fp, "volume_%d_size=%zu\n", i, vol->size);
        fprintf(fp, "volume_%d_driver=%s\n", i, vol->driver);
    }
    
    fclose(fp);
    return 0;
}

static int load_volume_metadata(struct hermit_volume_manager *mgr)
{
    char metadata_path[HERMIT_MAX_VOLUME_PATH];
    FILE *fp;
    char line[1024];
    
    snprintf(metadata_path, sizeof(metadata_path), "%s/volumes.metadata", mgr->base_path);
    
    fp = fopen(metadata_path, "r");
    if (!fp) {
        // No metadata file is OK for first run
        return 0;
    }
    
    mgr->volume_count = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL && mgr->volume_count < HERMIT_MAX_VOLUMES) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        
        // Parse volume entries
        if (strncmp(line, "volume_", 7) == 0) {
            char *key = strchr(line, '_');
            if (key) {
                key = strchr(key + 1, '_');
                if (key) {
                    int vol_index = atoi(line + 7);
                    if (vol_index >= 0 && vol_index < HERMIT_MAX_VOLUMES) {
                        struct hermit_volume *vol = &mgr->volumes[vol_index];
                        
                        if (strstr(key, "_name=")) {
                            char *value = strchr(key, '=') + 1;
                            value[strcspn(value, "\n")] = '\0';
                            strncpy(vol->name, value, sizeof(vol->name) - 1);
                        } else if (strstr(key, "_path=")) {
                            char *value = strchr(key, '=') + 1;
                            value[strcspn(value, "\n")] = '\0';
                            strncpy(vol->path, value, sizeof(vol->path) - 1);
                        } else if (strstr(key, "_ref_count=")) {
                            vol->ref_count = atoi(strchr(key, '=') + 1);
                        } else if (strstr(key, "_created_at=")) {
                            vol->created_at = atol(strchr(key, '=') + 1);
                        } else if (strstr(key, "_size=")) {
                            vol->size = atol(strchr(key, '=') + 1);
                        } else if (strstr(key, "_driver=")) {
                            char *value = strchr(key, '=') + 1;
                            value[strcspn(value, "\n")] = '\0';
                            strncpy(vol->driver, value, sizeof(vol->driver) - 1);
                        }
                        
                        if (vol_index >= mgr->volume_count) {
                            mgr->volume_count = vol_index + 1;
                        }
                    }
                }
            }
        }
    }
    
    fclose(fp);
    hermit_log(HERMIT_LOG_INFO, "volumes", "loaded %d volumes from metadata", mgr->volume_count);
    return 0;
}

int hermit_volume_manager_init(struct hermit_volume_manager *mgr, const char *base_path)
{
    if (!mgr || !base_path) {
        return -1;
    }
    
    memset(mgr, 0, sizeof(*mgr));
    strncpy(mgr->base_path, base_path, sizeof(mgr->base_path) - 1);
    
    // Create volumes directory
    if (mkdir(base_path, 0755) != 0 && errno != EEXIST) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "cannot create volumes directory: %s", strerror(errno));
        return -1;
    }
    
    // Load existing volume metadata
    load_volume_metadata(mgr);
    
    hermit_log(HERMIT_LOG_INFO, "volumes", "initialized volume manager at %s", base_path);
    return 0;
}

int hermit_volume_create(struct hermit_volume_manager *mgr, const char *name, const char *driver)
{
    char volume_path[HERMIT_MAX_VOLUME_PATH];
    struct hermit_volume *vol;
    
    if (!mgr || !name || mgr->volume_count >= HERMIT_MAX_VOLUMES) {
        return -1;
    }
    
    // Check if volume already exists
    if (hermit_volume_get(mgr, name, &vol) == 0) {
        hermit_log(HERMIT_LOG_WARN, "volumes", "volume %s already exists", name);
        return -1;
    }
    
    // Create volume directory
    snprintf(volume_path, sizeof(volume_path), "%s/%s", mgr->base_path, name);
    if (mkdir(volume_path, 0755) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "cannot create volume directory: %s", strerror(errno));
        return -1;
    }
    
    // Add to manager
    vol = &mgr->volumes[mgr->volume_count];
    strncpy(vol->name, name, sizeof(vol->name) - 1);
    strncpy(vol->path, volume_path, sizeof(vol->path) - 1);
    strncpy(vol->driver, driver ? driver : "local", sizeof(vol->driver) - 1);
    vol->ref_count = 0;
    vol->created_at = time(NULL);
    vol->size = 0;
    
    mgr->volume_count++;
    
    // Save metadata
    save_volume_metadata(mgr);
    
    hermit_log(HERMIT_LOG_INFO, "volumes", "created volume %s with driver %s", name, driver ? driver : "local");
    return 0;
}

int hermit_volume_remove(struct hermit_volume_manager *mgr, const char *name)
{
    struct hermit_volume *vol;
    char cmd[HERMIT_MAX_VOLUME_PATH];
    char resolved_path[HERMIT_MAX_VOLUME_PATH];
    
    if (!mgr || !name) {
        return -1;
    }
    
    if (hermit_volume_get(mgr, name, &vol) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "volume %s not found", name);
        return -1;
    }
    
    if (vol->ref_count > 0) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "cannot remove volume %s: %d references", 
                   name, vol->ref_count);
        return -1;
    }
    
    if (realpath(vol->path, resolved_path) == NULL) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "cannot resolve volume path: %s", strerror(errno));
        return -1;
    }
    
    const char *base_path = mgr->base_path;
    size_t base_len = strlen(base_path);
    if (strncmp(resolved_path, base_path, base_len) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "volume path escapes base directory");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "rm -rf %s", vol->path);
    if (system(cmd) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "failed to remove volume directory");
        return -1;
    }
    
    int index = vol - mgr->volumes;
    for (int i = index; i < mgr->volume_count - 1; i++) {
        mgr->volumes[i] = mgr->volumes[i + 1];
    }
    mgr->volume_count--;
    
    save_volume_metadata(mgr);
    
    hermit_log(HERMIT_LOG_INFO, "volumes", "removed volume %s", name);
    return 0;
}

int hermit_volume_get(struct hermit_volume_manager *mgr, const char *name, struct hermit_volume **volume)
{
    if (!mgr || !name || !volume) {
        return -1;
    }
    
    for (int i = 0; i < mgr->volume_count; i++) {
        if (strcmp(mgr->volumes[i].name, name) == 0) {
            *volume = &mgr->volumes[i];
            return 0;
        }
    }
    
    return -1;
}

int hermit_volume_ref(struct hermit_volume_manager *mgr, const char *name)
{
    struct hermit_volume *vol;
    
    if (hermit_volume_get(mgr, name, &vol) == 0) {
        vol->ref_count++;
        save_volume_metadata(mgr);
        hermit_log(HERMIT_LOG_DEBUG, "volumes", "referenced volume %s (ref_count=%d)", 
                   name, vol->ref_count);
        return 0;
    }
    
    return -1;
}

int hermit_volume_unref(struct hermit_volume_manager *mgr, const char *name)
{
    struct hermit_volume *vol;
    
    if (hermit_volume_get(mgr, name, &vol) == 0) {
        if (vol->ref_count > 0) {
            vol->ref_count--;
            save_volume_metadata(mgr);
            hermit_log(HERMIT_LOG_DEBUG, "volumes", "unreferenced volume %s (ref_count=%d)", 
                       name, vol->ref_count);
        }
        return 0;
    }
    
    return -1;
}

int hermit_volume_list(struct hermit_volume_manager *mgr, struct hermit_volume **volumes, int *count)
{
    if (!mgr || !volumes || !count) {
        return -1;
    }
    
    *volumes = mgr->volumes;
    *count = mgr->volume_count;
    
    return 0;
}

int hermit_volume_cleanup(struct hermit_volume_manager *mgr)
{
    if (!mgr) {
        return 0;
    }
    
    save_volume_metadata(mgr);
    
    memset(mgr, 0, sizeof(*mgr));
    
    hermit_log(HERMIT_LOG_INFO, "volumes", "cleaned up volume manager");
    return 0;
}

int hermit_volume_bind_mount(const char *source, const char *target, int readonly)
{
    char resolved_source[HERMIT_MAX_VOLUME_PATH];
    char resolved_target[HERMIT_MAX_VOLUME_PATH];
    char cmd[HERMIT_MAX_VOLUME_PATH * 2];
    
    if (!source || !target) {
        return -1;
    }
    
    if (realpath(source, resolved_source) == NULL) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "invalid source path: %s", strerror(errno));
        return -1;
    }
    
    if (realpath(target, resolved_target) == NULL) {
        if (strncmp(target, "/tmp/", 5) != 0 && strncmp(target, "/var/", 5) != 0) {
            hermit_log(HERMIT_LOG_ERROR, "volumes", "target must be in /tmp or /var");
            return -1;
        }
        strcpy(resolved_target, target);
    }
    
    if (strncmp(resolved_source, "/var/lib/hermit", 14) != 0 && 
        strncmp(resolved_source, "/home/", 6) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "source not in allowed directories");
        return -1;
    }
    
    if (strstr(source, "..") != NULL) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "path traversal not allowed");
        return -1;
    }
    
    if (mkdir(target, 0755) != 0 && errno != EEXIST) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "cannot create target mount point");
        return -1;
    }
    
    unsigned long flags = MS_BIND | MS_REC;
    if (readonly) {
        flags |= MS_RDONLY;
    }
    
    if (mount(resolved_source, target, NULL, flags, NULL) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "volumes", "bind mount failed: %s", strerror(errno));
        return -1;
    }
    
    hermit_log(HERMIT_LOG_INFO, "volumes", "bound mount %s -> %s (readonly=%d)", 
               source, target, readonly);
    return 0;
}
