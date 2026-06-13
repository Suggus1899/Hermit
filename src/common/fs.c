#include "hermit/common/fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int hermit_path_join2(char *dst, size_t dst_len, const char *a, const char *b)
{
    int n = snprintf(dst, dst_len, "%s/%s", a, b);
    if (n < 0 || (size_t)n >= dst_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int hermit_sanitize_image_ref(const char *ref, char *out, size_t out_len)
{
    size_t j = 0;

    for (size_t i = 0; ref[i] != '\0'; i++) {
        char c = ref[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                  c == ':';

        if (!ok) {
            c = '_';
        }

        if (j + 1 >= out_len) {
            errno = ENAMETOOLONG;
            return -1;
        }

        out[j++] = c;
    }

    if (j == 0) {
        errno = EINVAL;
        return -1;
    }

    out[j] = '\0';
    return 0;
}

int hermit_ensure_dir(const char *path, mode_t mode)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        return 0;
    }

    if (errno != ENOENT) {
        return -1;
    }

    if (mkdir(path, mode) != 0) {
        return -1;
    }

    return 0;
}

int hermit_init_data_root(hermit_app_context *ctx)
{
    const char *home = getenv("HOME");
    char path[PATH_MAX];

    if (home == NULL || home[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    if (snprintf(ctx->data_root, sizeof(ctx->data_root), "%s/.hermit", home) >=
        (int)sizeof(ctx->data_root)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (hermit_ensure_dir(ctx->data_root, 0700) != 0) {
        return -1;
    }

    if (hermit_path_join2(path, sizeof(path), ctx->data_root, HERMIT_PATH_CONTAINERS) != 0 ||
        hermit_ensure_dir(path, 0700) != 0) {
        return -1;
    }

    if (hermit_path_join2(path, sizeof(path), ctx->data_root, HERMIT_PATH_IMAGES) != 0 ||
        hermit_ensure_dir(path, 0700) != 0) {
        return -1;
    }

    if (hermit_path_join2(path, sizeof(path), ctx->data_root, HERMIT_PATH_VOLUMES) != 0 ||
        hermit_ensure_dir(path, 0700) != 0) {
        return -1;
    }

    if (hermit_path_join2(path, sizeof(path), ctx->data_root, HERMIT_PATH_REGISTRY) != 0 ||
        hermit_ensure_dir(path, 0700) != 0) {
        return -1;
    }

    if (hermit_path_join2(path, sizeof(path), ctx->data_root, HERMIT_PATH_ORCHESTRATOR) != 0 ||
        hermit_ensure_dir(path, 0700) != 0) {
        return -1;
    }

    return 0;
}

int hermit_list_dir_entries(const char *path)
{
    DIR *dir;
    struct dirent *ent;

    dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        printf("%s\n", ent->d_name);
    }

    if (closedir(dir) != 0) {
        return -1;
    }

    return 0;
}
