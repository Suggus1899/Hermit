#include "hermit/cli/commands.h"

#include "hermit/common/error.h"
#include "hermit/common/fs.h"
#include "hermit/common/log.h"
#include "hermit/runtime/run.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define HERMITD_SOCKET_PATH "/tmp/hermit.sock"

void hermit_cli_print_top_usage(const char *prog)
{
    fprintf(stderr,
            "Hermit - lightweight container runtime (docker-like CLI)\n"
            "\n"
            "Usage:\n"
            "  %s run <command> [args ...]\n"
            "  %s build -t <name:tag> <context>\n"
            "  %s images\n"
            "  %s image inspect <name>\n"
            "  %s ps\n"
            "  %s container create <name>\n"
            "  %s container start <name>\n"
            "  %s container stop <name>\n"
            "  %s container rm <name>\n"
            "  %s container inspect <name>\n"
            "  %s container ls\n"
            "  %s volume create <name>\n"
            "  %s volume ls\n"
            "  %s registry login <url>\n"
            "  %s orchestrate up <file>\n"
            "  %s inspect\n",
            prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

static int daemon_request_line(const char *cmd)
{
    int fd;
    struct sockaddr_un addr;
    char response[2048];
    ssize_t nread;
    size_t cmd_len = strlen(cmd);

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, HERMITD_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    if (write(fd, cmd, cmd_len) != (ssize_t)cmd_len || write(fd, "\n", 1) != 1) {
        close(fd);
        return -1;
    }

    nread = read(fd, response, sizeof(response) - 1);
    if (nread <= 0) {
        close(fd);
        return -1;
    }

    response[nread] = '\0';
    printf("%s", response);
    close(fd);
    return 0;
}

static int count_child_dirs(const char *path)
{
    int count = 0;
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
        count++;
    }

    if (closedir(dir) != 0) {
        return -1;
    }

    return count;
}

static int count_host_veth_like_links(void)
{
    char line[512];
    int count = 0;
    FILE *fp;

    fp = popen("ip -o link show", "r");
    if (fp == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, "@") != NULL) {
            count++;
        }
    }

    if (pclose(fp) != 0) {
        return -1;
    }

    return count;
}

static int cmd_inspect(void)
{
    int cgroup_children = -1;
    int veth_like_links = -1;

    if (access("/sys/fs/cgroup/hermit", F_OK) == 0) {
        cgroup_children = count_child_dirs("/sys/fs/cgroup/hermit");
    }

    if (access("/sbin/ip", X_OK) == 0 || access("/usr/sbin/ip", X_OK) == 0 ||
        access("/bin/ip", X_OK) == 0 || access("/usr/bin/ip", X_OK) == 0) {
        veth_like_links = count_host_veth_like_links();
    }

    printf("Hermit Inspect\n");
    if (cgroup_children >= 0) {
        printf("- cgroup hermit children: %d\n", cgroup_children);
    } else {
        printf("- cgroup hermit children: unavailable\n");
    }

    if (veth_like_links >= 0) {
        printf("- host links containing '@': %d\n", veth_like_links);
    } else {
        printf("- host links containing '@': unavailable\n");
    }

    if (cgroup_children > 0) {
        hermit_log(HERMIT_LOG_WARN, "inspect",
                   "possible leftover cgroup entries detected");
    }

    return 0;
}

static int cmd_images(hermit_app_context *ctx)
{
    char images_path[PATH_MAX];

    if (daemon_request_line("IMAGES.LIST") == 0) {
        return 0;
    }

    if (hermit_path_join2(images_path, sizeof(images_path), ctx->data_root,
                          HERMIT_PATH_IMAGES) != 0) {
        hermit_die_errno("hermit_path_join2(images)");
    }

    return hermit_list_dir_entries(images_path);
}

static int cmd_ps(hermit_app_context *ctx)
{
    char containers_path[PATH_MAX];

    if (daemon_request_line("CONTAINERS.LIST") == 0) {
        return 0;
    }

    if (hermit_path_join2(containers_path, sizeof(containers_path), ctx->data_root,
                          HERMIT_PATH_CONTAINERS) != 0) {
        hermit_die_errno("hermit_path_join2(containers)");
    }

    return hermit_list_dir_entries(containers_path);
}

static int cmd_volume_create(hermit_app_context *ctx, const char *name)
{
    char volumes_path[PATH_MAX];
    char volume_path[PATH_MAX];

    if (name == NULL || name[0] == '\0') {
        hermit_die_msg("volume create requires a name");
    }

    if (strchr(name, '/')) {
        hermit_die_msg("invalid volume name: '/' not allowed");
    }

    if (hermit_path_join2(volumes_path, sizeof(volumes_path), ctx->data_root,
                          HERMIT_PATH_VOLUMES) != 0) {
        hermit_die_errno("hermit_path_join2(volumes)");
    }

    if (hermit_path_join2(volume_path, sizeof(volume_path), volumes_path, name) != 0) {
        hermit_die_errno("hermit_path_join2(volume_name)");
    }

    if (mkdir(volume_path, 0700) != 0) {
        if (errno == EEXIST) {
            hermit_die_msg("volume '%s' already exists", name);
        }
        hermit_die_errno("mkdir(volume)");
    }

    printf("%s\n", name);
    return 0;
}

static int cmd_volume_ls(hermit_app_context *ctx)
{
    char volumes_path[PATH_MAX];

    if (hermit_path_join2(volumes_path, sizeof(volumes_path), ctx->data_root,
                          HERMIT_PATH_VOLUMES) != 0) {
        hermit_die_errno("hermit_path_join2(volumes)");
    }

    return hermit_list_dir_entries(volumes_path);
}

static int cmd_build(hermit_app_context *ctx, int argc, char **argv)
{
    const char *tag = NULL;
    const char *context = NULL;
    char safe_tag[PATH_MAX];
    char images_path[PATH_MAX];
    char image_meta[PATH_MAX];
    FILE *fp;
    time_t now;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                hermit_die_msg("build requires value after -t");
            }
            tag = argv[++i];
            continue;
        }
        context = argv[i];
    }

    if (tag == NULL || context == NULL) {
        hermit_die_msg("usage: %s build -t <name:tag> <context>", argv[0]);
    }

    if (hermit_sanitize_image_ref(tag, safe_tag, sizeof(safe_tag)) != 0) {
        hermit_die_errno("hermit_sanitize_image_ref");
    }

    if (hermit_path_join2(images_path, sizeof(images_path), ctx->data_root,
                          HERMIT_PATH_IMAGES) != 0) {
        hermit_die_errno("hermit_path_join2(images)");
    }

    if (snprintf(image_meta, sizeof(image_meta), "%s/%s.meta", images_path, safe_tag) >=
        (int)sizeof(image_meta)) {
        hermit_die_errno("snprintf(image_meta)");
    }

    fp = fopen(image_meta, "w");
    if (fp == NULL) {
        hermit_die_errno("fopen(image_meta)");
    }

    now = time(NULL);
    fprintf(fp, "tag=%s\n", tag);
    fprintf(fp, "context=%s\n", context);
    fprintf(fp, "created_at=%lld\n", (long long)now);
    fprintf(fp, "status=stub-build\n");

    if (fclose(fp) != 0) {
        hermit_die_errno("fclose(image_meta)");
    }

    printf("Built image metadata: %s\n", tag);
    return 0;
}

static int cmd_registry_login(hermit_app_context *ctx, const char *url)
{
    char registry_dir[PATH_MAX];
    char auth_file[PATH_MAX];
    FILE *fp;

    if (url == NULL || url[0] == '\0') {
        hermit_die_msg("usage: registry login <url>");
    }

    if (hermit_path_join2(registry_dir, sizeof(registry_dir), ctx->data_root,
                          HERMIT_PATH_REGISTRY) != 0) {
        hermit_die_errno("hermit_path_join2(registry)");
    }

    if (hermit_path_join2(auth_file, sizeof(auth_file), registry_dir, "auth.conf") != 0) {
        hermit_die_errno("hermit_path_join2(auth.conf)");
    }

    fp = fopen(auth_file, "w");
    if (fp == NULL) {
        hermit_die_errno("fopen(auth.conf)");
    }

    fprintf(fp, "registry_url=%s\n", url);
    fprintf(fp, "auth=placeholder\n");

    if (fclose(fp) != 0) {
        hermit_die_errno("fclose(auth.conf)");
    }

    printf("Registry login placeholder saved for %s\n", url);
    return 0;
}

static int cmd_orchestrate_up(const char *file)
{
    if (file == NULL || file[0] == '\0') {
        hermit_die_msg("usage: orchestrate up <file>");
    }

    hermit_log(HERMIT_LOG_WARN, "orchestrator",
               "stub for now; requested file: %s", file);
    return 0;
}

static int cmd_container_create(const char *name)
{
    char cmd[256];

    if (name == NULL || name[0] == '\0') {
        hermit_die_msg("usage: container create <name>");
    }

    if (snprintf(cmd, sizeof(cmd), "CONTAINERS.CREATE %s", name) >= (int)sizeof(cmd)) {
        hermit_die_msg("container name too long");
    }

    if (daemon_request_line(cmd) != 0) {
        hermit_die_msg("daemon unavailable for container create");
    }

    return 0;
}

static int cmd_container_rm(const char *name)
{
    char cmd[256];

    if (name == NULL || name[0] == '\0') {
        hermit_die_msg("usage: container rm <name>");
    }

    if (snprintf(cmd, sizeof(cmd), "CONTAINERS.DELETE %s", name) >= (int)sizeof(cmd)) {
        hermit_die_msg("container name too long");
    }

    if (daemon_request_line(cmd) != 0) {
        hermit_die_msg("daemon unavailable for container rm");
    }

    return 0;
}

static int cmd_container_ls(void)
{
    if (daemon_request_line("CONTAINERS.LIST") != 0) {
        hermit_die_msg("daemon unavailable for container ls");
    }
    return 0;
}

static int cmd_container_start(const char *name)
{
    char cmd[256];

    if (name == NULL || name[0] == '\0') {
        hermit_die_msg("usage: container start <name>");
    }

    if (snprintf(cmd, sizeof(cmd), "CONTAINERS.START %s", name) >= (int)sizeof(cmd)) {
        hermit_die_msg("container name too long");
    }

    if (daemon_request_line(cmd) != 0) {
        hermit_die_msg("daemon unavailable for container start");
    }

    return 0;
}

static int cmd_container_stop(const char *name)
{
    char cmd[256];

    if (name == NULL || name[0] == '\0') {
        hermit_die_msg("usage: container stop <name>");
    }

    if (snprintf(cmd, sizeof(cmd), "CONTAINERS.STOP %s", name) >= (int)sizeof(cmd)) {
        hermit_die_msg("container name too long");
    }

    if (daemon_request_line(cmd) != 0) {
        hermit_die_msg("daemon unavailable for container stop");
    }

    return 0;
}

static int cmd_container_inspect(const char *name)
{
    char cmd[256];

    if (name == NULL || name[0] == '\0') {
        hermit_die_msg("usage: container inspect <name>");
    }

    if (snprintf(cmd, sizeof(cmd), "CONTAINERS.INSPECT %s", name) >= (int)sizeof(cmd)) {
        hermit_die_msg("container name too long");
    }

    if (daemon_request_line(cmd) != 0) {
        hermit_die_msg("daemon unavailable for container inspect");
    }

    return 0;
}

static int cmd_image_inspect(const char *name)
{
    char cmd[256];

    if (name == NULL || name[0] == '\0') {
        hermit_die_msg("usage: image inspect <name>");
    }

    if (snprintf(cmd, sizeof(cmd), "IMAGES.INSPECT %s", name) >= (int)sizeof(cmd)) {
        hermit_die_msg("image name too long");
    }

    if (daemon_request_line(cmd) != 0) {
        hermit_die_msg("daemon unavailable for image inspect");
    }

    return 0;
}
int hermit_cli_dispatch(hermit_app_context *ctx, int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        hermit_cli_print_top_usage(argv[0]);
        return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "run") == 0) {
        return hermit_run_isolated_command(argc, argv);
    }

    if (strcmp(argv[1], "images") == 0) {
        if (argc != 2) {
            hermit_die_msg("usage: %s images", argv[0]);
        }
        return cmd_images(ctx);
    }

    if (strcmp(argv[1], "ps") == 0) {
        if (argc != 2) {
            hermit_die_msg("usage: %s ps", argv[0]);
        }
        return cmd_ps(ctx);
    }

    if (strcmp(argv[1], "build") == 0) {
        return cmd_build(ctx, argc, argv);
    }

    if (strcmp(argv[1], "volume") == 0) {
        if (argc >= 4 && strcmp(argv[2], "create") == 0) {
            return cmd_volume_create(ctx, argv[3]);
        }
        if (argc == 3 && strcmp(argv[2], "ls") == 0) {
            return cmd_volume_ls(ctx);
        }
        hermit_die_msg("usage: %s volume <create <name>|ls>", argv[0]);
    }

    if (strcmp(argv[1], "container") == 0) {
        if (argc == 4 && strcmp(argv[2], "create") == 0) {
            return cmd_container_create(argv[3]);
        }
        if (argc == 4 && strcmp(argv[2], "rm") == 0) {
            return cmd_container_rm(argv[3]);
        }
        if (argc == 4 && strcmp(argv[2], "start") == 0) {
            return cmd_container_start(argv[3]);
        }
        if (argc == 4 && strcmp(argv[2], "stop") == 0) {
            return cmd_container_stop(argv[3]);
        }
        if (argc == 4 && strcmp(argv[2], "inspect") == 0) {
            return cmd_container_inspect(argv[3]);
        }
        if (argc == 3 && strcmp(argv[2], "ls") == 0) {
            return cmd_container_ls();
        }
        hermit_die_msg("usage: %s container <create|rm|start|stop|inspect <name>|ls>", argv[0]);
    }

    if (strcmp(argv[1], "image") == 0) {
        if (argc == 4 && strcmp(argv[2], "inspect") == 0) {
            return cmd_image_inspect(argv[3]);
        }
        hermit_die_msg("usage: %s image inspect <name>", argv[0]);
    }

    if (strcmp(argv[1], "registry") == 0) {
        if (argc == 4 && strcmp(argv[2], "login") == 0) {
            return cmd_registry_login(ctx, argv[3]);
        }
        hermit_die_msg("usage: %s registry login <url>", argv[0]);
    }

    if (strcmp(argv[1], "orchestrate") == 0) {
        if (argc == 4 && strcmp(argv[2], "up") == 0) {
            return cmd_orchestrate_up(argv[3]);
        }
        hermit_die_msg("usage: %s orchestrate up <file>", argv[0]);
    }

    if (strcmp(argv[1], "inspect") == 0) {
        if (argc != 2) {
            hermit_die_msg("usage: %s inspect", argv[0]);
        }
        return cmd_inspect();
    }

    hermit_die_msg("unknown command '%s'", argv[1]);
    return EXIT_FAILURE;
}
