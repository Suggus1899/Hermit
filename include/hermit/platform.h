#ifndef HERMIT_PLATFORM_H
#define HERMIT_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

// Platform detection macros
#ifdef _WIN32
    #define HERMIT_PLATFORM_WINDOWS 1
    #define HERMIT_PLATFORM_UNIX 0
#elif __linux__
    #define HERMIT_PLATFORM_LINUX 1
    #define HERMIT_PLATFORM_UNIX 1
#elif __APPLE__
    #define HERMIT_PLATFORM_MACOS 1
    #define HERMIT_PLATFORM_UNIX 1
#else
    #define HERMIT_PLATFORM_UNIX 1
#endif

// WSL2 detection
#define HERMIT_WSL2_PROC_VERSION "/proc/version"
#define HERMIT_WSL2_SIGNATURE "Microsoft"
#define HERMIT_WSL2_SIGNATURE2 "WSL"

// Platform-specific paths
#ifdef HERMIT_PLATFORM_WINDOWS
    #define HERMIT_PATH_SEPARATOR "\\"
    #define HERMIT_PATH_SEPARATOR_CHAR '\\'
    #define HERMIT_PATH_LIST_SEPARATOR ";"
    #define HERMIT_EXECUTABLE_SUFFIX ".exe"
    #define HERMIT_LIBRARY_SUFFIX ".dll"
    #define HERMIT_STATE_DIR "C:\\ProgramData\\Hermit"
    #define HERMIT_CONFIG_DIR "C:\\ProgramData\\Hermit\\config"
    #define HERMIT_LOG_DIR "C:\\ProgramData\\Hermit\\logs"
    #define HERMIT_CACHE_DIR "C:\\ProgramData\\Hermit\\cache"
#else
    #define HERMIT_PATH_SEPARATOR "/"
    #define HERMIT_PATH_SEPARATOR_CHAR '/'
    #define HERMIT_PATH_LIST_SEPARATOR ":"
    #define HERMIT_EXECUTABLE_SUFFIX ""
    #define HERMIT_LIBRARY_SUFFIX ".so"
    #define HERMIT_STATE_DIR "/var/lib/hermit"
    #define HERMIT_CONFIG_DIR "/etc/hermit"
    #define HERMIT_LOG_DIR "/var/log/hermit"
    #define HERMIT_CACHE_DIR "/var/cache/hermit"
#endif

// Platform-specific limits
#define HERMIT_MAX_PATH 4096
#define HERMIT_MAX_CMDLINE 8192
#define HERMIT_MAX_ENV 1024

// Platform capabilities
struct hermit_platform_info {
    char name[64];
    char version[128];
    char architecture[64];
    bool is_wsl2;
    bool has_user_namespaces;
    bool has_cgroup_v2;
    bool has_overlayfs;
    bool has_seccomp;
    bool has_apparmor;
    bool has_selinux;
    bool has_systemd;
    size_t page_size;
    int max_pid;
    long max_open_files;
};

// Platform-specific optimizations
struct hermit_platform_config {
    bool wsl2_optimized;
    bool low_memory_mode;
    bool high_performance_mode;
    bool debug_mode;
    int worker_threads;
    size_t io_buffer_size;
    size_t network_buffer_size;
    int max_concurrent_containers;
};

// Platform detection functions
int hermit_platform_init(struct hermit_platform_info *info);
bool hermit_platform_is_wsl2(void);
bool hermit_platform_is_container(void);
bool hermit_platform_is_vm(void);

// Platform capability checks
bool hermit_platform_has_user_namespaces(void);
bool hermit_platform_has_cgroup_v2(void);
bool hermit_platform_has_overlayfs(void);
bool hermit_platform_has_seccomp(void);
bool hermit_platform_has_apparmor(void);
bool hermit_platform_has_selinux(void);
bool hermit_platform_has_systemd(void);

// Platform-specific optimizations
int hermit_platform_apply_optimizations(const struct hermit_platform_config *config);
int hermit_platform_wsl2_optimize_networking(void);
int hermit_platform_wsl2_optimize_storage(void);
int hermit_platform_wsl2_get_windows_path(const char *wsl_path, char *windows_path, size_t windows_path_size);

// Path utilities
int hermit_platform_normalize_path(char *path);
int hermit_platform_get_absolute_path(const char *relative_path, char *absolute_path, size_t absolute_path_size);
int hermit_platform_get_temp_path(char *temp_path, size_t temp_path_size);
int hermit_platform_get_home_path(char *home_path, size_t home_path_size);

// Process utilities
int hermit_platform_get_process_info(pid_t pid, char *comm, size_t comm_size);
int hermit_platform_get_process_cwd(pid_t pid, char *cwd, size_t cwd_size);
int hermit_platform_get_process_environ(pid_t pid, char **environ, int max_vars);

// System information
int hermit_platform_get_memory_info(size_t *total, size_t *available, size_t *free);
int hermit_platform_get_cpu_info(int *cores, double *load_average);
int hermit_platform_get_disk_info(const char *path, size_t *total, size_t *available, size_t *free);

// Platform-specific error handling
const char* hermit_platform_strerror(int error_code);
int hermit_platform_get_last_error(void);

#endif
