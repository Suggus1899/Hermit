#define _GNU_SOURCE
#include "hermit/daemon/server.h"

#include "hermit/common/log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define HERMITD_SOCKET_PATH "/tmp/hermit.sock"
#define HERMITD_BACKLOG 16
#define HERMITD_STATE_DIR "/tmp/hermitd-state"

static int g_state_lock_fd = -1;

static int is_valid_name(const char *name);

static int append_journal_line(const char *line)
{
    int fd;
    size_t len;

    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/journal.log", HERMITD_STATE_DIR) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        return -1;
    }

    len = strlen(line);
    if (write(fd, line, len) != (ssize_t)len || write(fd, "\n", 1) != 1) {
        close(fd);
        errno = EIO;
        return -1;
    }

    if (fsync(fd) != 0) {
        close(fd);
        return -1;
    }

    if (close(fd) != 0) {
        return -1;
    }

    return 0;
}

static int acquire_state_lock(void)
{
    if (g_state_lock_fd < 0) {
        char lock_path[PATH_MAX];
        if (snprintf(lock_path, sizeof(lock_path), "%s/state.lock", HERMITD_STATE_DIR) >=
            (int)sizeof(lock_path)) {
            errno = ENAMETOOLONG;
            return -1;
        }

        g_state_lock_fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (g_state_lock_fd < 0) {
            return -1;
        }
    }

    if (flock(g_state_lock_fd, LOCK_EX) != 0) {
        return -1;
    }

    return 0;
}

static void release_state_lock(void)
{
    if (g_state_lock_fd >= 0) {
        flock(g_state_lock_fd, LOCK_UN);
    }
}

static int ensure_dir(const char *path)
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

    if (mkdir(path, 0700) != 0) {
        return -1;
    }

    return 0;
}

static int init_state_dirs(void)
{
    char path[PATH_MAX];

    if (ensure_dir(HERMITD_STATE_DIR) != 0) {
        return -1;
    }

    if (snprintf(path, sizeof(path), "%s/containers", HERMITD_STATE_DIR) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (ensure_dir(path) != 0) {
        return -1;
    }

    if (snprintf(path, sizeof(path), "%s/images", HERMITD_STATE_DIR) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (ensure_dir(path) != 0) {
        return -1;
    }

    return 0;
}

static int apply_journal_entry(const char *line)
{
    if (strncmp(line, "CONTAINERS.CREATE ", 18) == 0) {
        const char *name = line + 18;
        char path[PATH_MAX];
        FILE *fp;

        if (!is_valid_name(name)) {
            return 0;
        }

        if (snprintf(path, sizeof(path), "%s/containers/%s.meta", HERMITD_STATE_DIR,
                     name) >= (int)sizeof(path)) {
            return -1;
        }

        fp = fopen(path, "w");
        if (fp == NULL) {
            return -1;
        }
        fprintf(fp, "name=%s\nstate=created\n", name);
        fclose(fp);
        return 0;
    }

    if (strncmp(line, "CONTAINERS.DELETE ", 18) == 0) {
        const char *name = line + 18;
        char path[PATH_MAX];

        if (!is_valid_name(name)) {
            return 0;
        }

        if (snprintf(path, sizeof(path), "%s/containers/%s.meta", HERMITD_STATE_DIR,
                     name) >= (int)sizeof(path)) {
            return -1;
        }

        unlink(path);
        return 0;
    }

    if (strncmp(line, "CONTAINERS.START ", 17) == 0) {
        const char *name = line + 17;
        char path[PATH_MAX];
        FILE *fp;

        if (!is_valid_name(name)) {
            return 0;
        }

        if (snprintf(path, sizeof(path), "%s/containers/%s.meta", HERMITD_STATE_DIR,
                     name) >= (int)sizeof(path)) {
            return -1;
        }

        fp = fopen(path, "w");
        if (fp == NULL) {
            return -1;
        }
        fprintf(fp, "name=%s\nstate=running\n", name);
        fclose(fp);
        return 0;
    }

    if (strncmp(line, "CONTAINERS.STOP ", 16) == 0) {
        const char *name = line + 16;
        char path[PATH_MAX];
        FILE *fp;

        if (!is_valid_name(name)) {
            return 0;
        }

        if (snprintf(path, sizeof(path), "%s/containers/%s.meta", HERMITD_STATE_DIR,
                     name) >= (int)sizeof(path)) {
            return -1;
        }

        fp = fopen(path, "w");
        if (fp == NULL) {
            return -1;
        }
        fprintf(fp, "name=%s\nstate=stopped\n", name);
        fclose(fp);
        return 0;
    }

    return 0;
}

static int replay_journal(void)
{
    char path[PATH_MAX];
    FILE *fp;
    char line[512];

    if (snprintf(path, sizeof(path), "%s/journal.log", HERMITD_STATE_DIR) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (apply_journal_entry(line) != 0) {
            fclose(fp);
            return -1;
        }
    }

    if (fclose(fp) != 0) {
        return -1;
    }

    return 0;
}

static int is_valid_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    for (size_t i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return 0;
        }
    }

    return 1;
}

static int write_json_ok(int fd, const char *msg)
{
    return dprintf(fd, "{\"status\":\"ok\",\"message\":\"%s\"}\n", msg);
}

static int write_json_err(int fd, const char *msg)
{
    return dprintf(fd, "{\"status\":\"error\",\"message\":\"%s\"}\n", msg);
}

static int handle_containers_create(int fd, const char *name)
{
    char path[PATH_MAX];
    FILE *fp;

    if (!is_valid_name(name)) {
        write_json_err(fd, "invalid container name");
        return 0;
    }

    if (snprintf(path, sizeof(path), "%s/containers/%s.meta", HERMITD_STATE_DIR,
                 name) >= (int)sizeof(path)) {
        write_json_err(fd, "path too long");
        return 0;
    }

    if (acquire_state_lock() != 0) {
        write_json_err(fd, "state lock failed");
        return 0;
    }

    fp = fopen(path, "w");
    if (fp == NULL) {
        release_state_lock();
        write_json_err(fd, "cannot create container metadata");
        return 0;
    }

    fprintf(fp, "name=%s\n", name);
    fprintf(fp, "state=created\n");
    fclose(fp);

    {
        char journal_cmd[256];
        if (snprintf(journal_cmd, sizeof(journal_cmd), "CONTAINERS.CREATE %s", name) <
            (int)sizeof(journal_cmd)) {
            append_journal_line(journal_cmd);
        }
    }

    release_state_lock();

    write_json_ok(fd, "container created");
    return 0;
}

static int handle_containers_delete(int fd, const char *name)
{
    char path[PATH_MAX];

    if (!is_valid_name(name)) {
        write_json_err(fd, "invalid container name");
        return 0;
    }

    if (snprintf(path, sizeof(path), "%s/containers/%s.meta", HERMITD_STATE_DIR,
                 name) >= (int)sizeof(path)) {
        write_json_err(fd, "path too long");
        return 0;
    }

    if (acquire_state_lock() != 0) {
        write_json_err(fd, "state lock failed");
        return 0;
    }

    if (unlink(path) != 0) {
        release_state_lock();
        write_json_err(fd, "container not found");
        return 0;
    }

    {
        char journal_cmd[256];
        if (snprintf(journal_cmd, sizeof(journal_cmd), "CONTAINERS.DELETE %s", name) <
            (int)sizeof(journal_cmd)) {
            append_journal_line(journal_cmd);
        }
    }

    release_state_lock();

    write_json_ok(fd, "container deleted");
    return 0;
}

static int handle_list_dir(int fd, const char *dirpath)
{
    DIR *dir;
    struct dirent *ent;

    dir = opendir(dirpath);
    if (dir == NULL) {
        write_json_err(fd, "cannot open state dir");
        return 0;
    }

    dprintf(fd, "{\"status\":\"ok\",\"items\":[");

    {
        int first = 1;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }

            if (!first) {
                dprintf(fd, ",");
            }
            dprintf(fd, "\"%s\"", ent->d_name);
            first = 0;
        }
    }

    dprintf(fd, "]}\n");
    closedir(dir);
    return 0;
}

static int handle_containers_start(int fd, const char *name)
{
    char path[PATH_MAX];
    char cmd[512];
    FILE *fp;

    if (!is_valid_name(name)) {
        write_json_err(fd, "invalid container name");
        return 0;
    }

    if (snprintf(path, sizeof(path), "%s/containers/%s.meta", HERMITD_STATE_DIR,
                 name) >= (int)sizeof(path)) {
        write_json_err(fd, "path too long");
        return 0;
    }

    if (acquire_state_lock() != 0) {
        write_json_err(fd, "state lock failed");
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        release_state_lock();
        write_json_err(fd, "container not found");
        return 0;
    }

    // Check current state
    char line[256];
    int found_state = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "state=", 6) == 0) {
            found_state = 1;
            if (strstr(line, "running")) {
                fclose(fp);
                release_state_lock();
                write_json_err(fd, "container already running");
                return 0;
            }
            break;
        }
    }
    fclose(fp);

    // Update state to running
    fp = fopen(path, "w");
    if (fp == NULL) {
        release_state_lock();
        write_json_err(fd, "cannot update container state");
        return 0;
    }

    fprintf(fp, "name=%s\n", name);
    fprintf(fp, "state=running\n");
    fclose(fp);

    {
        char journal_cmd[256];
        if (snprintf(journal_cmd, sizeof(journal_cmd), "CONTAINERS.START %s", name) <
            (int)sizeof(journal_cmd)) {
            append_journal_line(journal_cmd);
        }
    }

    release_state_lock();

    write_json_ok(fd, "container started");
    return 0;
}

static int handle_containers_stop(int fd, const char *name)
{
    char path[PATH_MAX];
    FILE *fp;

    if (!is_valid_name(name)) {
        write_json_err(fd, "invalid container name");
        return 0;
    }

    if (snprintf(path, sizeof(path), "%s/containers/%s.meta", HERMITD_STATE_DIR,
                 name) >= (int)sizeof(path)) {
        write_json_err(fd, "path too long");
        return 0;
    }

    if (acquire_state_lock() != 0) {
        write_json_err(fd, "state lock failed");
        return 0;
    }

    fp = fopen(path, "w");
    if (fp == NULL) {
        release_state_lock();
        write_json_err(fd, "container not found");
        return 0;
    }

    fprintf(fp, "name=%s\n", name);
    fprintf(fp, "state=stopped\n");
    fclose(fp);

    {
        char journal_cmd[256];
        if (snprintf(journal_cmd, sizeof(journal_cmd), "CONTAINERS.STOP %s", name) <
            (int)sizeof(journal_cmd)) {
            append_journal_line(journal_cmd);
        }
    }

    release_state_lock();

    write_json_ok(fd, "container stopped");
    return 0;
}

static int handle_containers_inspect(int fd, const char *name)
{
    char path[PATH_MAX];
    FILE *fp;
    char line[256];
    int found_container = 0;

    if (!is_valid_name(name)) {
        write_json_err(fd, "invalid container name");
        return 0;
    }

    if (snprintf(path, sizeof(path), "%s/containers/%s.meta", HERMITD_STATE_DIR,
                 name) >= (int)sizeof(path)) {
        write_json_err(fd, "path too long");
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        write_json_err(fd, "container not found");
        return 0;
    }

    dprintf(fd, "{\"status\":\"ok\",\"container\":{");
    
    int first = 1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strchr(line, '=')) {
            if (!first) {
                dprintf(fd, ",");
            }
            char *key = strtok(line, "=");
            char *value = strtok(NULL, "");
            dprintf(fd, "\"%s\":\"%s\"", key, value ? value : "");
            first = 0;
            found_container = 1;
        }
    }
    fclose(fp);

    dprintf(fd, "}}\n");
    return 0;
}

static int handle_images_inspect(int fd, const char *name)
{
    char path[PATH_MAX];
    FILE *fp;
    char line[256];
    int found_image = 0;

    if (!is_valid_name(name)) {
        write_json_err(fd, "invalid image name");
        return 0;
    }

    if (snprintf(path, sizeof(path), "%s/images/%s.meta", HERMITD_STATE_DIR,
                 name) >= (int)sizeof(path)) {
        write_json_err(fd, "path too long");
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        write_json_err(fd, "image not found");
        return 0;
    }

    dprintf(fd, "{\"status\":\"ok\",\"image\":{");
    
    int first = 1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strchr(line, '=')) {
            if (!first) {
                dprintf(fd, ",");
            }
            char *key = strtok(line, "=");
            char *value = strtok(NULL, "");
            dprintf(fd, "\"%s\":\"%s\"", key, value ? value : "");
            first = 0;
            found_image = 1;
        }
    }
    fclose(fp);

    dprintf(fd, "}}\n");
    return 0;
}
static int handle_client_request(int fd)
{
    char buf[512];
    ssize_t nread;

#ifdef SO_PEERCRED
    {
        struct ucred cred;
        socklen_t len = sizeof(cred);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
            if (cred.uid != getuid()) {
                write_json_err(fd, "unauthorized peer");
                return 0;
            }
        }
    }
#endif

    nread = read(fd, buf, sizeof(buf) - 1);
    if (nread <= 0) {
        return 0;
    }

    buf[nread] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';

    for (size_t i = 0; buf[i] != '\0'; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 32 || c > 126) {
            write_json_err(fd, "invalid command payload");
            return 0;
        }
    }

    if (strcmp(buf, "PING") == 0) {
        write_json_ok(fd, "pong");
        return 0;
    }

    if (strcmp(buf, "CONTAINERS.LIST") == 0) {
        return handle_list_dir(fd, HERMITD_STATE_DIR "/containers");
    }

    if (strcmp(buf, "IMAGES.LIST") == 0) {
        return handle_list_dir(fd, HERMITD_STATE_DIR "/images");
    }

    if (strncmp(buf, "CONTAINERS.CREATE ", 18) == 0) {
        return handle_containers_create(fd, buf + 18);
    }

    if (strncmp(buf, "CONTAINERS.DELETE ", 18) == 0) {
        return handle_containers_delete(fd, buf + 18);
    }

    if (strncmp(buf, "CONTAINERS.START ", 17) == 0) {
        return handle_containers_start(fd, buf + 17);
    }

    if (strncmp(buf, "CONTAINERS.STOP ", 16) == 0) {
        return handle_containers_stop(fd, buf + 16);
    }

    if (strncmp(buf, "CONTAINERS.INSPECT ", 19) == 0) {
        return handle_containers_inspect(fd, buf + 19);
    }

    if (strncmp(buf, "IMAGES.INSPECT ", 15) == 0) {
        return handle_images_inspect(fd, buf + 15);
    }

    write_json_err(fd, "unknown command");
    return 0;
}

int hermitd_run(void)
{
    int server_fd;
    struct sockaddr_un addr;

    server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        hermit_log(HERMIT_LOG_ERROR, "daemon", "socket failed: errno=%d (%s)",
                   errno, strerror(errno));
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, HERMITD_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (init_state_dirs() != 0) {
        hermit_log(HERMIT_LOG_ERROR, "daemon",
                   "init_state_dirs failed: errno=%d (%s)", errno,
                   strerror(errno));
        close(server_fd);
        return 1;
    }

    if (acquire_state_lock() != 0) {
        hermit_log(HERMIT_LOG_ERROR, "daemon",
                   "state lock failed at startup: errno=%d (%s)", errno,
                   strerror(errno));
        close(server_fd);
        return 1;
    }

    if (replay_journal() != 0) {
        release_state_lock();
        hermit_log(HERMIT_LOG_ERROR, "daemon",
                   "journal replay failed: errno=%d (%s)", errno,
                   strerror(errno));
        close(server_fd);
        return 1;
    }

    release_state_lock();

    unlink(HERMITD_SOCKET_PATH);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "daemon", "bind failed: errno=%d (%s)",
                   errno, strerror(errno));
        close(server_fd);
        return 1;
    }

    if (chmod(HERMITD_SOCKET_PATH, 0600) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "daemon", "chmod socket failed: errno=%d (%s)",
                   errno, strerror(errno));
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, HERMITD_BACKLOG) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "daemon", "listen failed: errno=%d (%s)",
                   errno, strerror(errno));
        close(server_fd);
        return 1;
    }

    hermit_log(HERMIT_LOG_INFO, "daemon",
               "hermitd listening on %s", HERMITD_SOCKET_PATH);

    for (;;) {
        int client_fd = accept4(server_fd, NULL, NULL, SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            hermit_log(HERMIT_LOG_ERROR, "daemon",
                       "accept4 failed: errno=%d (%s)", errno,
                       strerror(errno));
            close(server_fd);
            return 1;
        }

        handle_client_request(client_fd);
        close(client_fd);
    }

    return 0;
}
