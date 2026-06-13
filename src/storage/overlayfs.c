#define _GNU_SOURCE
#include "hermit/storage.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int hermit_storage_init(struct hermit_storage_driver *driver, const char *base_path)
{
    if (!driver || !base_path) {
        return -1;
    }
    
    memset(driver, 0, sizeof(*driver));
    strcpy(driver->name, "overlayfs");
    strncpy(driver->base_path, base_path, sizeof(driver->base_path) - 1);
    
    // Create storage directories
    char lower_dir[HERMIT_MAX_PATH];
    char upper_dir[HERMIT_MAX_PATH];
    char work_dir[HERMIT_MAX_PATH];
    
    snprintf(lower_dir, sizeof(lower_dir), "%s/lower", base_path);
    snprintf(upper_dir, sizeof(upper_dir), "%s/upper", base_path);
    snprintf(work_dir, sizeof(work_dir), "%s/work", base_path);
    
    if (mkdir(lower_dir, 0755) != 0 && errno != EEXIST) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "cannot create lower dir: %s", strerror(errno));
        return -1;
    }
    
    if (mkdir(upper_dir, 0755) != 0 && errno != EEXIST) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "cannot create upper dir: %s", strerror(errno));
        return -1;
    }
    
    if (mkdir(work_dir, 0755) != 0 && errno != EEXIST) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "cannot create work dir: %s", strerror(errno));
        return -1;
    }
    
    hermit_log(HERMIT_LOG_INFO, "storage", "initialized overlayfs storage at %s", base_path);
    return 0;
}

int hermit_storage_add_layer(struct hermit_storage_driver *driver, const char *digest, const char *path, bool readonly)
{
    if (!driver || !digest || !path || driver->layer_count >= HERMIT_MAX_LAYERS) {
        return -1;
    }
    
    struct hermit_storage_layer *layer = &driver->layers[driver->layer_count];
    
    strncpy(layer->digest, digest, sizeof(layer->digest) - 1);
    strncpy(layer->path, path, sizeof(layer->path) - 1);
    layer->is_readonly = readonly;
    
    // Get file size
    struct stat st;
    if (stat(path, &st) == 0) {
        layer->size = st.st_size;
    }
    
    driver->layer_count++;
    
    hermit_log(HERMIT_LOG_DEBUG, "storage", "added layer %s from %s", digest, path);
    return 0;
}

int hermit_storage_create_overlay(struct hermit_storage_driver *driver, const char *mount_path)
{
    char lower_dir[HERMIT_MAX_PATH];
    char upper_dir[HERMIT_MAX_PATH];
    char work_dir[HERMIT_MAX_PATH];
    char options[HERMIT_MAX_PATH];
    
    if (!driver || !mount_path) {
        return -1;
    }
    
    // Build overlay options
    snprintf(lower_dir, sizeof(lower_dir), "%s/lower", driver->base_path);
    snprintf(upper_dir, sizeof(upper_dir), "%s/upper", driver->base_path);
    snprintf(work_dir, sizeof(work_dir), "%s/work", driver->base_path);
    
    // Create mount point
    if (mkdir(mount_path, 0755) != 0 && errno != EEXIST) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "cannot create mount point %s: %s", 
                   mount_path, strerror(errno));
        return -1;
    }
    
    // Build overlay options string
    snprintf(options, sizeof(options), "lowerdir=%s,upperdir=%s,workdir=%s", 
             lower_dir, upper_dir, work_dir);
    
    // Mount overlayfs
    if (mount("overlay", mount_path, "overlay", 0, options) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "overlay mount failed: %s", strerror(errno));
        return -1;
    }
    
    strncpy(driver->current_mount, mount_path, sizeof(driver->current_mount) - 1);
    driver->mounted = true;
    
    hermit_log(HERMIT_LOG_INFO, "storage", "mounted overlay at %s", mount_path);
    return 0;
}

int hermit_storage_mount(struct hermit_storage_driver *driver, const char *mount_path)
{
    if (!driver || !mount_path) {
        return -1;
    }
    
    // For now, just create overlay
    return hermit_storage_create_overlay(driver, mount_path);
}

int hermit_storage_unmount(struct hermit_storage_driver *driver)
{
    if (!driver || !driver->mounted) {
        return 0;
    }
    
    if (umount(driver->current_mount) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "unmount failed: %s", strerror(errno));
        return -1;
    }
    
    driver->mounted = false;
    driver->current_mount[0] = '\0';
    
    hermit_log(HERMIT_LOG_INFO, "storage", "unmounted overlay");
    return 0;
}

int hermit_storage_commit_changes(struct hermit_storage_driver *driver, const char *new_digest)
{
    char upper_dir[HERMIT_MAX_PATH];
    char layer_path[HERMIT_MAX_PATH];
    char tar_path[HERMIT_MAX_PATH];
    char compressed_path[HERMIT_MAX_PATH];
    
    if (!driver || !new_digest || !driver->mounted) {
        return -1;
    }
    
    snprintf(upper_dir, sizeof(upper_dir), "%s/upper", driver->base_path);
    
    // Create tar archive of upper layer
    snprintf(tar_path, sizeof(tar_path), "%s/commit.tar", upper_dir);
    snprintf(compressed_path, sizeof(compressed_path), "%s/commit.tar.gz", upper_dir);
    
    // Simple tar creation (should use libarchive)
    char cmd[HERMIT_MAX_PATH];
    snprintf(cmd, sizeof(cmd), "cd %s && tar -cf %s .", upper_dir, tar_path);
    if (system(cmd) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "failed to create tar archive");
        return -1;
    }
    
    // Compress the tar
    snprintf(cmd, sizeof(cmd), "gzip %s", tar_path);
    if (system(cmd) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "failed to compress tar archive");
        return -1;
    }
    
    // Move to layers directory with digest name
    snprintf(layer_path, sizeof(layer_path), "%s/layers/%s.tar.gz", driver->base_path, new_digest + 7);
    snprintf(cmd, sizeof(cmd), "mv %s %s", compressed_path, layer_path);
    if (system(cmd) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "failed to move layer");
        return -1;
    }
    
    // Clear upper directory for next use
    snprintf(cmd, sizeof(cmd), "rm -rf %s/*", upper_dir);
    system(cmd);
    
    hermit_log(HERMIT_LOG_INFO, "storage", "committed changes as layer %s", new_digest);
    return 0;
}

int hermit_storage_cleanup(struct hermit_storage_driver *driver)
{
    if (!driver) {
        return 0;
    }
    
    if (driver->mounted) {
        hermit_storage_unmount(driver);
    }
    
    char cmd[HERMIT_MAX_PATH];
    snprintf(cmd, sizeof(cmd), "rm -rf %s/upper %s/work", driver->base_path, driver->base_path);
    system(cmd);
    
    memset(driver, 0, sizeof(*driver));
    
    hermit_log(HERMIT_LOG_INFO, "storage", "cleaned up storage driver");
    return 0;
}

int hermit_storage_detect_driver_type(enum hermit_storage_driver_type *type)
{
    FILE *fp;
    char line[256];
    
    if (!type) {
        return -1;
    }
    
    fp = fopen("/proc/filesystems", "r");
    if (!fp) {
        *type = HERMIT_STORAGE_DIRECT;
        return 0;
    }
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, "overlay") != NULL) {
            fclose(fp);
            *type = HERMIT_STORAGE_OVERLAYFS;
            hermit_log(HERMIT_LOG_INFO, "storage", "detected overlayfs support");
            return 0;
        }
    }
    
    fclose(fp);
    *type = HERMIT_STORAGE_DIRECT;
    hermit_log(HERMIT_LOG_INFO, "storage", "using direct storage (no overlayfs)");
    return 0;
}

int hermit_storage_copy_up(const char *source, const char *target)
{
    char cmd[HERMIT_MAX_PATH * 2];
    
    if (!source || !target) {
        return -1;
    }
    
    if (strstr(source, "..") != NULL || strstr(target, "..") != NULL) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "path traversal not allowed in copy_up");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "cp -a %s/. %s/", source, target);
    if (system(cmd) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "copy_up failed");
        return -1;
    }
    
    hermit_log(HERMIT_LOG_DEBUG, "storage", "copied up %s -> %s", source, target);
    return 0;
}

int hermit_storage_rootless_mount(struct hermit_storage_driver *driver, const char *mount_path)
{
    char lower_dir[HERMIT_MAX_PATH];
    char merged_dir[HERMIT_MAX_PATH];
    char cmd[HERMIT_MAX_PATH * 2];
    
    if (!driver || !mount_path) {
        return -1;
    }
    
    if (driver->layer_count == 0) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "no layers for rootless mount");
        return -1;
    }
    
    if (mkdir(mount_path, 0755) != 0 && errno != EEXIST) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "cannot create mount point");
        return -1;
    }
    
    snprintf(merged_dir, sizeof(merged_dir), "%s/merged", driver->base_path);
    if (mkdir(merged_dir, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    
    for (int i = driver->layer_count - 1; i >= 0; i--) {
        if (!driver->layers[i].is_readonly) {
            continue;
        }
        
        snprintf(cmd, sizeof(cmd), "cp -a %s/. %s/", driver->layers[i].path, merged_dir);
        system(cmd);
    }
    
    if (driver->layer_count > 0) {
        struct hermit_storage_layer *rw_layer = &driver->layers[driver->layer_count - 1];
        if (!rw_layer->is_readonly) {
            snprintf(cmd, sizeof(cmd), "cp -a %s/. %s/", rw_layer->path, merged_dir);
            system(cmd);
        }
    }
    
    snprintf(cmd, sizeof(cmd), "mount --bind %s %s", merged_dir, mount_path);
    if (system(cmd) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "storage", "rootless bind mount failed");
        return -1;
    }
    
    strncpy(driver->current_mount, mount_path, sizeof(driver->current_mount) - 1);
    driver->mounted = true;
    
    hermit_log(HERMIT_LOG_INFO, "storage", "rootless mount at %s", mount_path);
    return 0;
}

static const char *hermit_layer_usage_file = "/tmp/hermitd-state/layer_usage.db";

int hermit_storage_record_layer_usage(const char *layer_digest, const char *image_name)
{
    FILE *fp;
    char line[512];
    char temp_file[256];
    
    if (!layer_digest || !image_name) {
        return -1;
    }
    
    snprintf(temp_file, sizeof(temp_file), "%s.tmp", hermit_layer_usage_file);
    
    fp = fopen(hermit_layer_usage_file, "a");
    if (!fp) {
        fp = fopen(temp_file, "w");
        if (!fp) {
            return -1;
        }
    }
    
    fprintf(fp, "%s %s %ld\n", layer_digest, image_name, (long)time(NULL));
    fclose(fp);
    
    hermit_log(HERMIT_LOG_DEBUG, "storage", "recorded layer usage: %s by %s", layer_digest, image_name);
    return 0;
}

int hermit_storage_get_layer_usage_count(const char *layer_digest)
{
    FILE *fp;
    char line[512];
    char digest[HERMIT_MAX_DIGEST_LEN];
    int count = 0;
    
    if (!layer_digest) {
        return -1;
    }
    
    fp = fopen(hermit_layer_usage_file, "r");
    if (!fp) {
        return 0;
    }
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (sscanf(line, "%s", digest) == 1) {
            if (strcmp(digest, layer_digest) == 0) {
                count++;
            }
        }
    }
    
    fclose(fp);
    return count;
}

int hermit_storage_gc_orphaned_layers(const char *layers_dir, const char *images_dir)
{
    DIR *dir, *img_dir;
    struct dirent *ent, *img_ent;
    char layer_path[HERMIT_MAX_PATH];
    char image_manifest[HERMIT_MAX_PATH];
    char used_digests[1024][HERMIT_MAX_DIGEST_LEN];
    int used_count = 0;
    int removed = 0;
    
    if (!layers_dir || !images_dir) {
        return -1;
    }
    
    img_dir = opendir(images_dir);
    if (!img_dir) {
        return -1;
    }
    
    while ((img_ent = readdir(img_dir)) != NULL) {
        if (strcmp(img_ent->d_name, ".") == 0 || strcmp(img_ent->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(image_manifest, sizeof(image_manifest), "%s/%s/manifest.json", images_dir, img_ent->d_name);
        
        FILE *mf = fopen(image_manifest, "r");
        if (!mf) {
            continue;
        }
        
        char manifest_data[16384];
        size_t len = fread(manifest_data, 1, sizeof(manifest_data) - 1, mf);
        manifest_data[len] = '\0';
        fclose(mf);
        
        char *ptr = manifest_data;
        while ((ptr = strstr(ptr, "\"digest\"")) != NULL) {
            ptr = strchr(ptr, '"') + 1;
            char *end = strchr(ptr, '"');
            if (end && used_count < 1024) {
                size_t copy_len = end - ptr;
                if (copy_len < HERMIT_MAX_DIGEST_LEN) {
                    strncpy(used_digests[used_count], ptr, copy_len);
                    used_digests[used_count][copy_len] = '\0';
                    used_count++;
                }
            }
            ptr = end;
        }
    }
    
    closedir(img_dir);
    
    dir = opendir(layers_dir);
    if (!dir) {
        return -1;
    }
    
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        
        if (strstr(ent->d_name, ".tar.gz") == NULL) {
            continue;
        }
        
        char digest_str[HERMIT_MAX_DIGEST_LEN];
        strcpy(digest_str, "sha256:");
        strncat(digest_str, ent->d_name, sizeof(digest_str) - 7);
        
        int in_use = 0;
        for (int i = 0; i < used_count; i++) {
            if (strstr(used_digests[i], digest_str + 7) != NULL) {
                in_use = 1;
                break;
            }
        }
        
        if (!in_use) {
            snprintf(layer_path, sizeof(layer_path), "%s/%s", layers_dir, ent->d_name);
            unlink(layer_path);
            removed++;
            hermit_log(HERMIT_LOG_INFO, "storage", "removed orphaned layer: %s", ent->d_name);
        }
    }
    
    closedir(dir);
    hermit_log(HERMIT_LOG_INFO, "storage", "GC removed %d orphaned layers", removed);
    return removed;
}
