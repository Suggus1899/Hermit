#define _GNU_SOURCE
#include "hermit/runtime/run.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define HERMIT_CHILD_STACK_SIZE (1024 * 1024)
#define HERMIT_MAX_FD_SCAN 65536

enum hermit_child_stage {
    HERMIT_CHILD_STAGE_CLOSE_FDS = 1,
    HERMIT_CHILD_STAGE_SANITIZE_ENV = 2,
    HERMIT_CHILD_STAGE_VERIFY_PID1 = 3,
    HERMIT_CHILD_STAGE_SET_HOSTNAME = 4,
    HERMIT_CHILD_STAGE_ROOTFS = 5,
    HERMIT_CHILD_STAGE_SYNC_PARENT = 6,
    HERMIT_CHILD_STAGE_EXEC = 7,
    HERMIT_CHILD_STAGE_INIT = 8,
};

struct hermit_child_error {
    int stage;
    int errnum;
};

struct hermit_child_config {
    char **argv;
    int error_fd;
    int start_fd;
    const char *hostname;
    const char *rootfs;
    bool mount_sys;
    bool minimal_dev;
    bool sys_read_only;
};

struct hermit_runtime_config {
    char **child_argv;
    const char *hostname;
    const char *rootfs;
    const char *memory_max;
    const char *pids_max;
    const char *cpu_max;
    const char *io_max;
    bool enable_veth;
    const char *veth_host_cidr;
    const char *veth_child_cidr;
    const char *veth_gateway;
    bool enable_nat;
    const char *nat_out_if;
    bool mount_sys;
    bool minimal_dev;
    bool sys_read_only;
    bool enable_userns;
    uid_t host_uid;
    gid_t host_gid;
    int clone_flags;
    size_t child_stack_size;
    int error_pipe[2];
    int start_pipe[2];
    char cgroup_path[4096];
    bool cgroup_created;
    char veth_host_if[16];
    bool veth_created;
    bool nat_rules_applied;
};

struct hermit_signal_state {
    struct sigaction old_int;
    struct sigaction old_term;
    bool installed;
};

static volatile sig_atomic_t g_parent_interrupted = 0;
static volatile sig_atomic_t g_parent_signal = 0;
static volatile sig_atomic_t g_active_child_pid = -1;
static volatile sig_atomic_t g_init_payload_pid = -1;

static const char *stage_name(int stage)
{
    switch (stage) {
    case HERMIT_CHILD_STAGE_CLOSE_FDS:
        return "close_extra_fds";
    case HERMIT_CHILD_STAGE_SANITIZE_ENV:
        return "sanitize_environment";
    case HERMIT_CHILD_STAGE_VERIFY_PID1:
        return "verify_pid1";
    case HERMIT_CHILD_STAGE_SET_HOSTNAME:
        return "sethostname";
    case HERMIT_CHILD_STAGE_ROOTFS:
        return "setup_rootfs";
    case HERMIT_CHILD_STAGE_SYNC_PARENT:
        return "wait_parent_sync";
    case HERMIT_CHILD_STAGE_EXEC:
        return "execvp";
    case HERMIT_CHILD_STAGE_INIT:
        return "init_supervisor";
    default:
        return "unknown";
    }
}

static int close_extra_fds(int preserve_fd_a, int preserve_fd_b)
{
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0) {
        max_fd = HERMIT_MAX_FD_SCAN;
    }

    for (int fd = 3; fd < max_fd; fd++) {
        if (fd == preserve_fd_a || fd == preserve_fd_b) {
            continue;
        }

        if (close(fd) == -1 && errno != EBADF) {
            return -1;
        }
    }

    return 0;
}

static void report_child_error(int fd, int stage)
{
    struct hermit_child_error err_msg;

    err_msg.stage = stage;
    err_msg.errnum = errno;

    if (write(fd, &err_msg, sizeof(err_msg)) < 0) {
        return;
    }
}

static int sanitize_environment(void)
{
    if (clearenv() != 0) {
        return -1;
    }

    if (setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1) != 0) {
        return -1;
    }

    if (setenv("TERM", "xterm-256color", 1) != 0) {
        return -1;
    }

    return 0;
}

static int setup_minimal_dev_nodes(void)
{
    if (mkdir("/dev", 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME,
              "mode=755,size=16m") != 0) {
        return -1;
    }

    if (mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3)) != 0 && errno != EEXIST) {
        return -1;
    }

    if (mknod("/dev/zero", S_IFCHR | 0666, makedev(1, 5)) != 0 && errno != EEXIST) {
        return -1;
    }

    if (mknod("/dev/random", S_IFCHR | 0666, makedev(1, 8)) != 0 && errno != EEXIST) {
        return -1;
    }

    if (mknod("/dev/urandom", S_IFCHR | 0666, makedev(1, 9)) != 0 && errno != EEXIST) {
        return -1;
    }

    if (mkdir("/dev/shm", 01777) != 0 && errno != EEXIST) {
        return -1;
    }

    unlink("/dev/fd");
    if (symlink("/proc/self/fd", "/dev/fd") != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static int setup_sys_mount(bool sys_read_only)
{
    unsigned long flags = MS_NOSUID | MS_NODEV | MS_NOEXEC;

    if (mkdir("/sys", 0555) != 0 && errno != EEXIST) {
        return -1;
    }

    if (mount("sysfs", "/sys", "sysfs", flags, NULL) != 0) {
        return -1;
    }

    if (sys_read_only) {
        if (mount(NULL, "/sys", NULL,
                  MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC,
                  NULL) != 0) {
            return -1;
        }
    }

    return 0;
}

static int setup_rootfs(const struct hermit_child_config *cfg)
{
    const char *rootfs = cfg->rootfs;

    if (rootfs == NULL || rootfs[0] == '\0') {
        return 0;
    }

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        return -1;
    }

    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) != 0) {
        return -1;
    }

    if (chdir(rootfs) != 0) {
        return -1;
    }

    if (mkdir(".old_root", 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    if (syscall(SYS_pivot_root, ".", ".old_root") != 0) {
        return -1;
    }

    if (chdir("/") != 0) {
        return -1;
    }

    if (umount2("/.old_root", MNT_DETACH) != 0) {
        return -1;
    }

    if (rmdir("/.old_root") != 0) {
        return -1;
    }

    if (mkdir("/proc", 0555) != 0 && errno != EEXIST) {
        return -1;
    }

    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC,
              NULL) != 0) {
        return -1;
    }

    if (cfg->mount_sys) {
        if (setup_sys_mount(cfg->sys_read_only) != 0) {
            return -1;
        }
    }

    if (cfg->minimal_dev) {
        if (setup_minimal_dev_nodes() != 0) {
            return -1;
        }
    }

    return 0;
}

static int wait_parent_signal(int fd)
{
    char token;
    ssize_t nread = read(fd, &token, 1);

    if (nread < 0) {
        return -1;
    }

    if (nread == 0) {
        errno = EPIPE;
        return -1;
    }

    return 0;
}

static int run_command_argv(char *const argv[])
{
    int status;
    pid_t pid = fork();

    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int run_in_netns(pid_t target_pid, char *const argv[])
{
    int status;
    pid_t helper = fork();

    if (helper < 0) {
        return -1;
    }

    if (helper == 0) {
        int nsfd;
        char ns_path[128];

        if (snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/net", target_pid) >=
            (int)sizeof(ns_path)) {
            _exit(126);
        }

        nsfd = open(ns_path, O_RDONLY | O_CLOEXEC);
        if (nsfd < 0) {
            _exit(125);
        }

        if (setns(nsfd, CLONE_NEWNET) != 0) {
            close(nsfd);
            _exit(124);
        }

        close(nsfd);
        execvp(argv[0], argv);
        _exit(123);
    }

    if (waitpid(helper, &status, 0) < 0) {
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int cleanup_runtime_resources(struct hermit_runtime_config *cfg)
{
    int retry_count = 0;
    const int max_retries = 3;
    int cleanup_errors = 0;

    hermit_log(HERMIT_LOG_DEBUG, "runtime", "starting resource cleanup with retries=%d", max_retries);

    for (retry_count = 0; retry_count < max_retries; retry_count++) {
        cleanup_errors = 0;

        if (retry_count > 0) {
            hermit_log(HERMIT_LOG_INFO, "runtime", "cleanup retry %d/%d", retry_count + 1, max_retries);
            usleep(100000 * retry_count); // 100ms, 200ms, 300ms delays
        }

        if (cfg->nat_rules_applied && cfg->enable_nat && cfg->nat_out_if != NULL) {
            char *cmd[] = {"iptables", "-t", "nat", "-D", "POSTROUTING", "-s",
                           (char *)cfg->veth_child_cidr, "-o", (char *)cfg->nat_out_if,
                           "-j", "MASQUERADE", NULL};
            if (run_command_argv(cmd) != 0) {
                hermit_log(HERMIT_LOG_WARN, "runtime",
                           "failed to cleanup nat rule: errno=%d (%s)", errno,
                           strerror(errno));
                cleanup_errors++;
            } else {
                cfg->nat_rules_applied = false;
                hermit_log(HERMIT_LOG_DEBUG, "runtime", "nat rule cleaned successfully");
            }
        }

        if (cfg->veth_created) {
            char *cmd[] = {"ip", "link", "del", cfg->veth_host_if, NULL};
            if (run_command_argv(cmd) != 0) {
                hermit_log(HERMIT_LOG_WARN, "runtime",
                           "failed to cleanup veth host interface %s: errno=%d (%s)",
                           cfg->veth_host_if, errno, strerror(errno));
                cleanup_errors++;
            } else {
                cfg->veth_created = false;
                hermit_log(HERMIT_LOG_DEBUG, "runtime", "veth interface %s cleaned successfully", cfg->veth_host_if);
            }
        }

        if (cfg->cgroup_created) {
            if (rmdir(cfg->cgroup_path) != 0) {
                hermit_log(HERMIT_LOG_WARN, "runtime",
                           "failed to cleanup cgroup %s: errno=%d (%s)",
                           cfg->cgroup_path, errno, strerror(errno));
                cleanup_errors++;
            } else {
                cfg->cgroup_created = false;
                hermit_log(HERMIT_LOG_DEBUG, "runtime", "cgroup %s cleaned successfully", cfg->cgroup_path);
            }
        }

        if (cleanup_errors == 0) {
            hermit_log(HERMIT_LOG_INFO, "runtime", "all resources cleaned successfully on attempt %d", retry_count + 1);
            return 0;
        }
    }

    hermit_log(HERMIT_LOG_ERROR, "runtime", "cleanup failed after %d retries with %d errors remaining", 
               max_retries, cleanup_errors);
    
    return -1;
}

static void parent_signal_handler(int sig)
{
    g_parent_interrupted = 1;
    g_parent_signal = sig;

    if (g_active_child_pid > 0) {
        kill((pid_t)g_active_child_pid, sig);
    }
}

static void init_signal_handler(int sig)
{
    pid_t payload_pid = (pid_t)g_init_payload_pid;

    if (payload_pid > 0) {
        kill(-payload_pid, sig);
    }
}

static int install_init_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = init_signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return -1;
    }

    if (sigaction(SIGHUP, &sa, NULL) != 0) {
        return -1;
    }

    if (sigaction(SIGQUIT, &sa, NULL) != 0) {
        return -1;
    }

    return 0;
}

static int run_init_supervisor(char **argv, int error_fd)
{
    int status;
    pid_t payload_pid;

    if (install_init_signal_handlers() != 0) {
        report_child_error(error_fd, HERMIT_CHILD_STAGE_INIT);
        return 132;
    }

    payload_pid = fork();
    if (payload_pid < 0) {
        report_child_error(error_fd, HERMIT_CHILD_STAGE_INIT);
        return 132;
    }

    if (payload_pid == 0) {
        setpgid(0, 0);
        execvp(argv[0], argv);
        _exit(127);
    }

    g_init_payload_pid = payload_pid;

    for (;;) {
        pid_t reaped = waitpid(-1, &status, 0);
        if (reaped < 0) {
            if (errno == EINTR) {
                continue;
            }
            report_child_error(error_fd, HERMIT_CHILD_STAGE_INIT);
            return 133;
        }

        if (reaped != payload_pid) {
            continue;
        }

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }

        if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }

        return 134;
    }
}

static int install_parent_signal_handlers(struct hermit_signal_state *state)
{
    struct sigaction sa;

    memset(state, 0, sizeof(*state));
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = parent_signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, &state->old_int) != 0) {
        return -1;
    }

    if (sigaction(SIGTERM, &sa, &state->old_term) != 0) {
        sigaction(SIGINT, &state->old_int, NULL);
        return -1;
    }

    state->installed = true;
    return 0;
}

static void restore_parent_signal_handlers(struct hermit_signal_state *state)
{
    if (!state->installed) {
        return;
    }

    sigaction(SIGINT, &state->old_int, NULL);
    sigaction(SIGTERM, &state->old_term, NULL);
    state->installed = false;
}

static int write_text_file(const char *path, const char *value)
{
    int fd;
    size_t len;
    ssize_t nwritten;

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    len = strlen(value);
    nwritten = write(fd, value, len);
    if (nwritten < 0 || (size_t)nwritten != len) {
        close(fd);
        errno = EIO;
        return -1;
    }

    if (close(fd) != 0) {
        return -1;
    }

    return 0;
}

static int configure_user_namespace(struct hermit_runtime_config *cfg, pid_t child_pid)
{
    char path[256];
    char map[128];

    if (!cfg->enable_userns) {
        return 0;
    }

    if (snprintf(path, sizeof(path), "/proc/%d/setgroups", child_pid) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (write_text_file(path, "deny") != 0) {
        return -1;
    }

    if (snprintf(path, sizeof(path), "/proc/%d/uid_map", child_pid) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (snprintf(map, sizeof(map), "0 %u 1", (unsigned int)cfg->host_uid) >=
        (int)sizeof(map)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (write_text_file(path, map) != 0) {
        return -1;
    }

    if (snprintf(path, sizeof(path), "/proc/%d/gid_map", child_pid) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (snprintf(map, sizeof(map), "0 %u 1", (unsigned int)cfg->host_gid) >=
        (int)sizeof(map)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (write_text_file(path, map) != 0) {
        return -1;
    }

    return 0;
}

static int apply_cgroup_v2_limits(struct hermit_runtime_config *cfg, pid_t child_pid)
{
    char path[4096];
    char pid_str[64];

    if (cfg->memory_max == NULL && cfg->pids_max == NULL && cfg->cpu_max == NULL &&
        cfg->io_max == NULL) {
        return 0;
    }

    if (mkdir("/sys/fs/cgroup/hermit", 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    if (snprintf(cfg->cgroup_path, sizeof(cfg->cgroup_path), "/sys/fs/cgroup/hermit/%d",
                 child_pid) >= (int)sizeof(cfg->cgroup_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (mkdir(cfg->cgroup_path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    if (cfg->memory_max != NULL) {
        if (snprintf(path, sizeof(path), "%s/memory.max", cfg->cgroup_path) >=
            (int)sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }

        if (write_text_file(path, cfg->memory_max) != 0) {
            return -1;
        }
    }

    if (cfg->pids_max != NULL) {
        if (snprintf(path, sizeof(path), "%s/pids.max", cfg->cgroup_path) >=
            (int)sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }

        if (write_text_file(path, cfg->pids_max) != 0) {
            return -1;
        }
    }

    if (cfg->cpu_max != NULL) {
        if (snprintf(path, sizeof(path), "%s/cpu.max", cfg->cgroup_path) >=
            (int)sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }

        if (write_text_file(path, cfg->cpu_max) != 0) {
            return -1;
        }
    }

    if (cfg->io_max != NULL) {
        if (snprintf(path, sizeof(path), "%s/io.max", cfg->cgroup_path) >=
            (int)sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }

        if (write_text_file(path, cfg->io_max) != 0) {
            return -1;
        }
    }

    if (snprintf(path, sizeof(path), "%s/cgroup.procs", cfg->cgroup_path) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (snprintf(pid_str, sizeof(pid_str), "%d", child_pid) >= (int)sizeof(pid_str)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (write_text_file(path, pid_str) != 0) {
        return -1;
    }

    cfg->cgroup_created = true;
    return 0;
}

static int setup_veth_network(struct hermit_runtime_config *cfg, pid_t child_pid)
{
    char host_if[16];
    char child_if[16];
    char child_pid_str[32];

    if (!cfg->enable_veth) {
        return 0;
    }

    cfg->veth_host_if[0] = '\0';

    if (snprintf(host_if, sizeof(host_if), "h%dh", child_pid % 100000) >=
        (int)sizeof(host_if) ||
        snprintf(child_if, sizeof(child_if), "h%dc", child_pid % 100000) >=
            (int)sizeof(child_if) ||
        snprintf(child_pid_str, sizeof(child_pid_str), "%d", child_pid) >=
            (int)sizeof(child_pid_str)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    {
        char *cmd[] = {"ip", "link", "add", host_if, "type", "veth", "peer",
                       "name", child_if, NULL};
        if (run_command_argv(cmd) != 0) {
            return -1;
        }
    }

    snprintf(cfg->veth_host_if, sizeof(cfg->veth_host_if), "%s", host_if);
    cfg->veth_created = true;

    {
        char *cmd[] = {"ip", "link", "set", child_if, "netns", child_pid_str,
                       NULL};
        if (run_command_argv(cmd) != 0) {
            return -1;
        }
    }

    {
        char *cmd[] = {"ip", "addr", "add", (char *)cfg->veth_host_cidr, "dev",
                       host_if, NULL};
        if (run_command_argv(cmd) != 0) {
            return -1;
        }
    }

    {
        char *cmd[] = {"ip", "link", "set", host_if, "up", NULL};
        if (run_command_argv(cmd) != 0) {
            return -1;
        }
    }

    {
        char *cmd[] = {"ip", "link", "set", "lo", "up", NULL};
        if (run_in_netns(child_pid, cmd) != 0) {
            return -1;
        }
    }

    {
        char *cmd[] = {"ip", "addr", "add", (char *)cfg->veth_child_cidr, "dev",
                       child_if, NULL};
        if (run_in_netns(child_pid, cmd) != 0) {
            return -1;
        }
    }

    {
        char *cmd[] = {"ip", "link", "set", child_if, "up", NULL};
        if (run_in_netns(child_pid, cmd) != 0) {
            return -1;
        }
    }

    if (cfg->veth_gateway != NULL) {
        char *cmd[] = {"ip", "route", "add", "default", "via",
                       (char *)cfg->veth_gateway, NULL};
        if (run_in_netns(child_pid, cmd) != 0) {
            return -1;
        }
    }

    if (cfg->enable_nat && cfg->nat_out_if != NULL) {
        {
            char *cmd[] = {"sysctl", "-w", "net.ipv4.ip_forward=1", NULL};
            if (run_command_argv(cmd) != 0) {
                return -1;
            }
        }

        {
            char *cmd[] = {"iptables", "-t", "nat", "-A", "POSTROUTING", "-s",
                           (char *)cfg->veth_child_cidr, "-o",
                           (char *)cfg->nat_out_if, "-j", "MASQUERADE", NULL};
            if (run_command_argv(cmd) != 0) {
                return -1;
            }
        }

        cfg->nat_rules_applied = true;
    }

    return 0;
}

static int child_main(void *arg)
{
    struct hermit_child_config *cfg = arg;
    pid_t pid;

    if (close_extra_fds(cfg->error_fd, cfg->start_fd) != 0) {
        report_child_error(cfg->error_fd, HERMIT_CHILD_STAGE_CLOSE_FDS);
        return 125;
    }

    if (sanitize_environment() != 0) {
        report_child_error(cfg->error_fd, HERMIT_CHILD_STAGE_SANITIZE_ENV);
        return 126;
    }

    pid = getpid();
    if (pid != 1) {
        errno = EPERM;
        report_child_error(cfg->error_fd, HERMIT_CHILD_STAGE_VERIFY_PID1);
        return 127;
    }

    if (cfg->hostname != NULL && cfg->hostname[0] != '\0') {
        if (sethostname(cfg->hostname, strlen(cfg->hostname)) != 0) {
            report_child_error(cfg->error_fd, HERMIT_CHILD_STAGE_SET_HOSTNAME);
            return 128;
        }
    }

    if (setup_rootfs(cfg) != 0) {
        report_child_error(cfg->error_fd, HERMIT_CHILD_STAGE_ROOTFS);
        return 129;
    }

    if (wait_parent_signal(cfg->start_fd) != 0) {
        report_child_error(cfg->error_fd, HERMIT_CHILD_STAGE_SYNC_PARENT);
        return 130;
    }

    return run_init_supervisor(cfg->argv, cfg->error_fd);
}

static void usage_run(const char *prog)
{
    fprintf(stderr,
            "Usage: %s run [--hostname <name>] [--rootfs <path>]\n"
            "              [--memory-max <bytes>] [--pids-max <count>]\n"
            "              [--cpu-max <quota period>] [--io-max <rule>]\n"
            "              [--userns]\n"
            "              [--mount-sys] [--sys-rw] [--no-minimal-dev]\n"
            "              [--net-veth] [--host-ip-cidr <cidr>]\n"
            "              [--net-nat --out-if <iface>]\n"
            "              [--child-ip-cidr <cidr>] [--gateway <ip>]\n"
            "              <command> [args ...]\n"
            "\n"
            "Runs a command in Hermit's isolated process context.\n",
            prog);
}

static void init_runtime_config(struct hermit_runtime_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->clone_flags = SIGCHLD | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET |
                       CLONE_NEWUTS;
    cfg->child_stack_size = HERMIT_CHILD_STACK_SIZE;
    cfg->hostname = "hermit";
    cfg->rootfs = NULL;
    cfg->memory_max = NULL;
    cfg->pids_max = NULL;
    cfg->cpu_max = NULL;
    cfg->io_max = NULL;
    cfg->enable_veth = false;
    cfg->enable_nat = false;
    cfg->nat_out_if = NULL;
    cfg->mount_sys = false;
    cfg->minimal_dev = true;
    cfg->sys_read_only = true;
    cfg->enable_userns = false;
    cfg->host_uid = getuid();
    cfg->host_gid = getgid();
    cfg->veth_host_cidr = "10.20.0.1/24";
    cfg->veth_child_cidr = "10.20.0.2/24";
    cfg->veth_gateway = "10.20.0.1";
    cfg->error_pipe[0] = -1;
    cfg->error_pipe[1] = -1;
    cfg->start_pipe[0] = -1;
    cfg->start_pipe[1] = -1;
    cfg->cgroup_created = false;
    cfg->cgroup_path[0] = '\0';
    cfg->veth_created = false;
    cfg->veth_host_if[0] = '\0';
    cfg->nat_rules_applied = false;
}

static bool parse_args(int argc, char **argv, struct hermit_runtime_config *cfg)
{
    int argi = 2;

    while (argi < argc) {
        if (strcmp(argv[argi], "--hostname") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->hostname = argv[argi + 1];
            argi += 2;
            continue;
        }

        if (strcmp(argv[argi], "--rootfs") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->rootfs = argv[argi + 1];
            argi += 2;
            continue;
        }

        if (strcmp(argv[argi], "--memory-max") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->memory_max = argv[argi + 1];
            argi += 2;
            continue;
        }

        if (strcmp(argv[argi], "--cpu-max") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->cpu_max = argv[argi + 1];
            argi += 2;
            continue;
        }

        if (strcmp(argv[argi], "--io-max") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->io_max = argv[argi + 1];
            argi += 2;
            continue;
        }

        if (strcmp(argv[argi], "--pids-max") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->pids_max = argv[argi + 1];
            argi += 2;
            continue;
        }

        if (strcmp(argv[argi], "--net-veth") == 0) {
            cfg->enable_veth = true;
            argi += 1;
            continue;
        }

        if (strcmp(argv[argi], "--userns") == 0) {
            cfg->enable_userns = true;
            argi += 1;
            continue;
        }

        if (strcmp(argv[argi], "--mount-sys") == 0) {
            cfg->mount_sys = true;
            argi += 1;
            continue;
        }

        if (strcmp(argv[argi], "--sys-rw") == 0) {
            cfg->sys_read_only = false;
            argi += 1;
            continue;
        }

        if (strcmp(argv[argi], "--no-minimal-dev") == 0) {
            cfg->minimal_dev = false;
            argi += 1;
            continue;
        }

        if (strcmp(argv[argi], "--host-ip-cidr") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->veth_host_cidr = argv[argi + 1];
            argi += 2;
            continue;
        }

        if (strcmp(argv[argi], "--net-nat") == 0) {
            cfg->enable_nat = true;
            argi += 1;
            continue;
        }

        if (strcmp(argv[argi], "--out-if") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->nat_out_if = argv[argi + 1];
            argi += 2;
            continue;
        }

        if (strcmp(argv[argi], "--child-ip-cidr") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->veth_child_cidr = argv[argi + 1];
            argi += 2;
            continue;
        }

        if (strcmp(argv[argi], "--gateway") == 0) {
            if (argi + 1 >= argc) {
                return false;
            }

            cfg->veth_gateway = argv[argi + 1];
            argi += 2;
            continue;
        }

        break;
    }

    if (argi >= argc) {
        return false;
    }

    if (cfg->enable_nat && (!cfg->enable_veth || cfg->nat_out_if == NULL)) {
        return false;
    }

    cfg->child_argv = &argv[argi];
    return true;
}

static void *allocate_child_stack(size_t stack_size)
{
    void *stack;

    stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        hermit_die_errno("mmap(child_stack)");
    }

    return stack;
}

static void destroy_child_stack(void *stack, size_t stack_size)
{
    if (stack != NULL && stack != MAP_FAILED) {
        if (munmap(stack, stack_size) != 0) {
            hermit_die_errno("munmap(child_stack)");
        }
    }
}

static void close_fd_if_open(int *fd)
{
    if (*fd >= 0) {
        if (close(*fd) != 0) {
            hermit_die_errno("close(fd)");
        }
        *fd = -1;
    }
}

int hermit_run_isolated_command(int argc, char **argv)
{
    int status;
    int interrupted_exit = 130;
    pid_t child_pid;
    ssize_t nread;
    struct hermit_child_config cfg;
    struct hermit_child_error child_err;
    struct hermit_runtime_config rt_cfg;
    struct hermit_signal_state signal_state;
    void *child_stack;
    char *stack_top;

    init_runtime_config(&rt_cfg);

    if (!parse_args(argc, argv, &rt_cfg)) {
        usage_run(argv[0]);
        return EXIT_FAILURE;
    }

    g_parent_interrupted = 0;
    g_parent_signal = 0;
    g_active_child_pid = -1;

    if (install_parent_signal_handlers(&signal_state) != 0) {
        hermit_die_errno("install_parent_signal_handlers");
    }

    if (pipe2(rt_cfg.error_pipe, O_CLOEXEC) != 0) {
        hermit_die_errno("pipe2(error_pipe)");
    }

    if (pipe2(rt_cfg.start_pipe, O_CLOEXEC) != 0) {
        close_fd_if_open(&rt_cfg.error_pipe[0]);
        close_fd_if_open(&rt_cfg.error_pipe[1]);
        hermit_die_errno("pipe2(start_pipe)");
    }

    child_stack = allocate_child_stack(rt_cfg.child_stack_size);
    stack_top = (char *)child_stack + rt_cfg.child_stack_size;

    cfg.argv = rt_cfg.child_argv;
    cfg.error_fd = rt_cfg.error_pipe[1];
    cfg.start_fd = rt_cfg.start_pipe[0];
    cfg.hostname = rt_cfg.hostname;
    cfg.rootfs = rt_cfg.rootfs;
    cfg.mount_sys = rt_cfg.mount_sys;
    cfg.minimal_dev = rt_cfg.minimal_dev;
    cfg.sys_read_only = rt_cfg.sys_read_only;

    if (rt_cfg.enable_userns) {
        rt_cfg.clone_flags |= CLONE_NEWUSER;
    }

    hermit_log(HERMIT_LOG_INFO, "runtime",
               "starting container process with clone flags=0x%x hostname=%s rootfs=%s",
               rt_cfg.clone_flags, cfg.hostname != NULL ? cfg.hostname : "(none)",
               cfg.rootfs != NULL ? cfg.rootfs : "(host)");

    child_pid = clone(child_main, stack_top, rt_cfg.clone_flags, &cfg);
    if (child_pid < 0) {
        restore_parent_signal_handlers(&signal_state);
        close_fd_if_open(&rt_cfg.error_pipe[0]);
        close_fd_if_open(&rt_cfg.error_pipe[1]);
        close_fd_if_open(&rt_cfg.start_pipe[0]);
        close_fd_if_open(&rt_cfg.start_pipe[1]);
        destroy_child_stack(child_stack, rt_cfg.child_stack_size);
        hermit_die_errno("clone");
    }

    g_active_child_pid = child_pid;

    close_fd_if_open(&rt_cfg.error_pipe[1]);
    close_fd_if_open(&rt_cfg.start_pipe[0]);

    hermit_log(HERMIT_LOG_INFO, "runtime", "child pid=%d created", child_pid);

    if (apply_cgroup_v2_limits(&rt_cfg, child_pid) != 0) {
        restore_parent_signal_handlers(&signal_state);
        close_fd_if_open(&rt_cfg.error_pipe[0]);
        close_fd_if_open(&rt_cfg.start_pipe[1]);
        destroy_child_stack(child_stack, rt_cfg.child_stack_size);
        if (cleanup_runtime_resources(&rt_cfg) != 0) {
            hermit_log(HERMIT_LOG_WARN, "runtime", "partial cleanup failure during cgroup setup error");
        }
        hermit_die_errno("apply_cgroup_v2_limits");
    }

    if (configure_user_namespace(&rt_cfg, child_pid) != 0) {
        restore_parent_signal_handlers(&signal_state);
        close_fd_if_open(&rt_cfg.error_pipe[0]);
        close_fd_if_open(&rt_cfg.start_pipe[1]);
        destroy_child_stack(child_stack, rt_cfg.child_stack_size);
        if (cleanup_runtime_resources(&rt_cfg) != 0) {
            hermit_log(HERMIT_LOG_WARN, "runtime", "partial cleanup failure during userns setup error");
        }
        hermit_die_errno("configure_user_namespace");
    }

    if (rt_cfg.cgroup_created) {
        hermit_log(HERMIT_LOG_INFO, "runtime", "cgroup attached at %s",
                   rt_cfg.cgroup_path);
    }

    if (setup_veth_network(&rt_cfg, child_pid) != 0) {
        restore_parent_signal_handlers(&signal_state);
        close_fd_if_open(&rt_cfg.error_pipe[0]);
        close_fd_if_open(&rt_cfg.start_pipe[1]);
        destroy_child_stack(child_stack, rt_cfg.child_stack_size);
        if (cleanup_runtime_resources(&rt_cfg) != 0) {
            hermit_log(HERMIT_LOG_WARN, "runtime", "partial cleanup failure during veth setup error");
        }
        hermit_die_errno("setup_veth_network");
    }

    {
        const char token = '1';
        if (write(rt_cfg.start_pipe[1], &token, 1) != 1) {
            restore_parent_signal_handlers(&signal_state);
            close_fd_if_open(&rt_cfg.error_pipe[0]);
            close_fd_if_open(&rt_cfg.start_pipe[1]);
            destroy_child_stack(child_stack, rt_cfg.child_stack_size);
            if (cleanup_runtime_resources(&rt_cfg) != 0) {
                hermit_log(HERMIT_LOG_WARN, "runtime", "partial cleanup failure during start pipe write error");
            }
            hermit_die_errno("write(start_pipe)");
        }
    }

    close_fd_if_open(&rt_cfg.start_pipe[1]);

    do {
        nread = read(rt_cfg.error_pipe[0], &child_err, sizeof(child_err));
    } while (nread < 0 && errno == EINTR && !g_parent_interrupted);

    if (nread < 0) {
        restore_parent_signal_handlers(&signal_state);
        close_fd_if_open(&rt_cfg.error_pipe[0]);
        destroy_child_stack(child_stack, rt_cfg.child_stack_size);
        if (cleanup_runtime_resources(&rt_cfg) != 0) {
            hermit_log(HERMIT_LOG_WARN, "runtime", "partial cleanup failure during error pipe read error");
        }
        hermit_die_errno("read(error_pipe)");
    }

    if (nread == (ssize_t)sizeof(child_err)) {
        hermit_log(HERMIT_LOG_ERROR, "runtime",
                   "child setup failed in %s: errno=%d (%s)",
                   stage_name(child_err.stage), child_err.errnum,
                   strerror(child_err.errnum));
    } else if (nread != 0) {
        restore_parent_signal_handlers(&signal_state);
        close_fd_if_open(&rt_cfg.error_pipe[0]);
        destroy_child_stack(child_stack, rt_cfg.child_stack_size);
        if (cleanup_runtime_resources(&rt_cfg) != 0) {
            hermit_log(HERMIT_LOG_WARN, "runtime", "partial cleanup failure during unexpected error pipe size");
        }
        hermit_die_msg("unexpected size from error pipe: %zd", nread);
    }

    close_fd_if_open(&rt_cfg.error_pipe[0]);

    while (waitpid(child_pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }

        restore_parent_signal_handlers(&signal_state);
        destroy_child_stack(child_stack, rt_cfg.child_stack_size);
        if (cleanup_runtime_resources(&rt_cfg) != 0) {
            hermit_log(HERMIT_LOG_WARN, "runtime", "partial cleanup failure during waitpid error");
        }
        hermit_die_errno("waitpid");
    }

    destroy_child_stack(child_stack, rt_cfg.child_stack_size);
    if (cleanup_runtime_resources(&rt_cfg) != 0) {
        hermit_log(HERMIT_LOG_WARN, "runtime", "partial cleanup failure during normal termination");
    }
    restore_parent_signal_handlers(&signal_state);
    g_active_child_pid = -1;

    if (g_parent_interrupted) {
        hermit_log(HERMIT_LOG_WARN, "runtime",
                   "execution interrupted by signal %d", (int)g_parent_signal);
        return interrupted_exit;
    }

    if (WIFEXITED(status)) {
        hermit_log(HERMIT_LOG_INFO, "runtime",
                   "container process exited with status=%d", WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        hermit_die_msg("child terminated by signal %d (%s)", sig, strsignal(sig));
    }

    hermit_die_msg("child exited unexpectedly");
    return EXIT_FAILURE;
}
