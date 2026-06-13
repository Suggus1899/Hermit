#define _GNU_SOURCE
#include "hermit/platform.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <errno.h>

#ifdef HERMIT_PLATFORM_LINUX
#include <sys/sysinfo.h>
#include <sys/statfs.h>
#include <fcntl.h>
#endif

#ifdef HERMIT_PLATFORM_WINDOWS
#include <windows.h>
#include <shlobj.h>
#endif

static struct hermit_platform_info g_platform_info = {0};
static bool g_platform_initialized = false;

int hermit_platform_init(struct hermit_platform_info *info)
{
    struct utsname uts;
    FILE *fp;
    char line[1024];
    
    if (!info) {
        return -1;
    }
    
    memset(info, 0, sizeof(*info));
    
    // Get basic system information
    if (uname(&uts) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "platform", "uname failed: %s", strerror(errno));
        return -1;
    }
    
    strncpy(info->name, uts.sysname, sizeof(info->name) - 1);
    strncpy(info->version, uts.release, sizeof(info->version) - 1);
    strncpy(info->architecture, uts.machine, sizeof(info->architecture) - 1);
    
    // Get page size
    info->page_size = sysconf(_SC_PAGESIZE);
    
    // Get system limits
    info->max_pid = sysconf(_SC_MAX_PID);
    info->max_open_files = sysconf(_SC_OPEN_MAX);
    
    // Check for WSL2
    info->is_wsl2 = hermit_platform_is_wsl2();
    
    // Check platform capabilities
    info->has_user_namespaces = hermit_platform_has_user_namespaces();
    info->has_cgroup_v2 = hermit_platform_has_cgroup_v2();
    info->has_overlayfs = hermit_platform_has_overlayfs();
    info->has_seccomp = hermit_platform_has_seccomp();
    info->has_apparmor = hermit_platform_has_apparmor();
    info->has_selinux = hermit_platform_has_selinux();
    info->has_systemd = hermit_platform_has_systemd();
    
    g_platform_info = *info;
    g_platform_initialized = true;
    
    hermit_log(HERMIT_LOG_INFO, "platform", "initialized platform: %s %s (%s)", 
               info->name, info->version, info->architecture);
    
    return 0;
}

bool hermit_platform_is_wsl2(void)
{
    FILE *fp;
    char line[1024];
    
    fp = fopen(HERMIT_WSL2_PROC_VERSION, "r");
    if (!fp) {
        return false;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, HERMIT_WSL2_SIGNATURE) || strstr(line, HERMIT_WSL2_SIGNATURE2)) {
            fclose(fp);
            return true;
        }
    }
    
    fclose(fp);
    return false;
}

bool hermit_platform_is_container(void)
{
    FILE *fp;
    char line[1024];
    
    // Check for container indicators
    fp = fopen("/proc/1/cgroup", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "docker") || strstr(line, "containerd") || 
                strstr(line, "lxc") || strstr(line, "kubepods")) {
                fclose(fp);
                return true;
            }
        }
        fclose(fp);
    }
    
    // Check for .dockerenv file
    if (access("/.dockerenv", F_OK) == 0) {
        return true;
    }
    
    return false;
}

bool hermit_platform_is_vm(void)
{
    FILE *fp;
    char line[1024];
    
    // Check DMI product name for virtualization indicators
    fp = fopen("/sys/class/dmi/id/product_name", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "VMware") || strstr(line, "VirtualBox") || 
                strstr(line, "QEMU") || strstr(line, "KVM") ||
                strstr(line, "Xen") || strstr(line, "Hyper-V")) {
                fclose(fp);
                return true;
            }
        }
        fclose(fp);
    }
    
    // Check hypervisor through CPUID (simplified check)
    fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "hypervisor") || strstr(line, "QEMU") || 
                strstr(line, "VMware") || strstr(line, "Xen")) {
                fclose(fp);
                return true;
            }
        }
        fclose(fp);
    }
    
    return false;
}

bool hermit_platform_has_user_namespaces(void)
{
#ifdef HERMIT_PLATFORM_LINUX
    int uid_map_fd;
    
    uid_map_fd = open("/proc/self/uid_map", O_RDONLY);
    if (uid_map_fd >= 0) {
        close(uid_map_fd);
        return true;
    }
#endif
    return false;
}

bool hermit_platform_has_cgroup_v2(void)
{
#ifdef HERMIT_PLATFORM_LINUX
    FILE *fp;
    char line[1024];
    
    fp = fopen("/proc/cgroups", "r");
    if (!fp) {
        return false;
    }
    
    // Skip header
    fgets(line, sizeof(line), fp);
    
    while (fgets(line, sizeof(line), fp)) {
        int hierarchy_id, num_cgroups, enabled;
        char name[256];
        
        if (sscanf(line, "%s %d %d %d", name, &hierarchy_id, &num_cgroups, &enabled) == 4) {
            if (strcmp(name, "unified") == 0 && hierarchy_id != 0) {
                fclose(fp);
                return true;
            }
        }
    }
    
    fclose(fp);
    
    // Alternative check: look for cgroup v2 mount
    fp = fopen("/proc/mounts", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "cgroup2") || (strstr(line, "cgroup") && strstr(line, "unified"))) {
                fclose(fp);
                return true;
            }
        }
        fclose(fp);
    }
#endif
    return false;
}

bool hermit_platform_has_overlayfs(void)
{
#ifdef HERMIT_PLATFORM_LINUX
    FILE *fp;
    char line[1024];
    
    fp = fopen("/proc/filesystems", "r");
    if (!fp) {
        return false;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "overlay")) {
            fclose(fp);
            return true;
        }
    }
    
    fclose(fp);
#endif
    return false;
}

bool hermit_platform_has_seccomp(void)
{
#ifdef HERMIT_PLATFORM_LINUX
    // Check if seccomp is available
    if (prctl(PR_GET_SECCOMP, 0, 0, 0, 0) >= 0 || errno == EINVAL) {
        return true;
    }
#endif
    return false;
}

bool hermit_platform_has_apparmor(void)
{
#ifdef HERMIT_PLATFORM_LINUX
    // Check for AppArmor
    if (access("/sys/kernel/security/apparmor", F_OK) == 0) {
        return true;
    }
#endif
    return false;
}

bool hermit_platform_has_selinux(void)
{
#ifdef HERMIT_PLATFORM_LINUX
    // Check for SELinux
    if (access("/sys/fs/selinux", F_OK) == 0) {
        return true;
    }
#endif
    return false;
}

bool hermit_platform_has_systemd(void)
{
#ifdef HERMIT_PLATFORM_LINUX
    // Check for systemd
    if (access("/run/systemd/system", F_OK) == 0) {
        return true;
    }
    
    // Alternative check: look for systemd process
    FILE *fp = fopen("/proc/1/comm", "r");
    if (fp) {
        char comm[256];
        if (fgets(comm, sizeof(comm), fp)) {
            comm[strcspn(comm, "\n")] = '\0';
            if (strcmp(comm, "systemd") == 0) {
                fclose(fp);
                return true;
            }
        }
        fclose(fp);
    }
#endif
    return false;
}

int hermit_platform_apply_optimizations(const struct hermit_platform_config *config)
{
    if (!config) {
        return -1;
    }
    
    hermit_log(HERMIT_LOG_INFO, "platform", "applying platform optimizations");
    
    if (config->wsl2_optimized && g_platform_info.is_wsl2) {
        hermit_log(HERMIT_LOG_INFO, "platform", "enabling WSL2 optimizations");
        
        if (hermit_platform_wsl2_optimize_networking() != 0) {
            hermit_log(HERMIT_LOG_WARN, "platform", "failed to optimize WSL2 networking");
        }
        
        if (hermit_platform_wsl2_optimize_storage() != 0) {
            hermit_log(HERMIT_LOG_WARN, "platform", "failed to optimize WSL2 storage");
        }
    }
    
    // Apply memory optimizations
    if (config->low_memory_mode) {
        hermit_log(HERMIT_LOG_INFO, "platform", "enabling low memory mode");
        // TODO: Implement low memory optimizations
    }
    
    // Apply performance optimizations
    if (config->high_performance_mode) {
        hermit_log(HERMIT_LOG_INFO, "platform", "enabling high performance mode");
        // TODO: Implement high performance optimizations
    }
    
    return 0;
}

int hermit_platform_wsl2_optimize_networking(void)
{
    FILE *fp;
    char line[1024];
    
    if (!g_platform_info.is_wsl2) {
        return -1;
    }
    
    hermit_log(HERMIT_LOG_DEBUG, "platform", "optimizing WSL2 networking");
    
    // Check if we can optimize network buffer sizes
    fp = fopen("/proc/sys/net/core/rmem_max", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            int rmem_max = atoi(line);
            hermit_log(HERMIT_LOG_DEBUG, "platform", "current rmem_max: %d", rmem_max);
            
            // Suggest increasing buffer sizes for better performance
            if (rmem_max < 16777216) { // 16MB
                hermit_log(HERMIT_LOG_INFO, "platform", "consider increasing rmem_max for better network performance");
            }
        }
        fclose(fp);
    }
    
    // Check WSL2 specific network settings
    fp = fopen("/proc/sys/net/ipv4/tcp_congestion_control", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = '\0';
            hermit_log(HERMIT_LOG_DEBUG, "platform", "TCP congestion control: %s", line);
        }
        fclose(fp);
    }
    
    return 0;
}

int hermit_platform_wsl2_optimize_storage(void)
{
    FILE *fp;
    char line[1024];
    
    if (!g_platform_info.is_wsl2) {
        return -1;
    }
    
    hermit_log(HERMIT_LOG_DEBUG, "platform", "optimizing WSL2 storage");
    
    // Check file system type
    fp = fopen("/proc/mounts", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, " / ")) {
                char *fs_type = strrchr(line, ' ');
                if (fs_type) {
                    fs_type[strcspn(fs_type, "\n")] = '\0';
                    hermit_log(HERMIT_LOG_DEBUG, "platform", "root filesystem type: %s", fs_type);
                    
                    // WSL2 typically uses ext4 or drvfs
                    if (strstr(fs_type, "drvfs")) {
                        hermit_log(HERMIT_LOG_INFO, "platform", "detected Windows filesystem (drvfs), performance may be limited");
                    }
                }
                break;
            }
        }
        fclose(fp);
    }
    
    // Check I/O scheduler
    fp = fopen("/sys/block/sda/queue/scheduler", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = '\0';
            hermit_log(HERMIT_LOG_DEBUG, "platform", "I/O scheduler: %s", line);
        }
        fclose(fp);
    }
    
    return 0;
}

int hermit_platform_wsl2_get_windows_path(const char *wsl_path, char *windows_path, size_t windows_path_size)
{
    FILE *fp;
    char line[1024];
    char cmd[4096];
    
    if (!wsl_path || !windows_path || !g_platform_info.is_wsl2) {
        return -1;
    }
    
    // Use wslpath utility to convert WSL path to Windows path
    snprintf(cmd, sizeof(cmd), "wslpath -w '%s' 2>/dev/null", wsl_path);
    
    fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }
    
    if (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        strncpy(windows_path, line, windows_path_size - 1);
        windows_path[windows_path_size - 1] = '\0';
        
        pclose(fp);
        hermit_log(HERMIT_LOG_DEBUG, "platform", "converted WSL path '%s' to Windows path '%s'", 
                   wsl_path, windows_path);
        return 0;
    }
    
    pclose(fp);
    return -1;
}

int hermit_platform_normalize_path(char *path)
{
    char *src, *dst;
    char last_char = '\0';
    
    if (!path) {
        return -1;
    }
    
    src = dst = path;
    
    while (*src) {
        // Handle path separators
        if (*src == '/' || *src == '\\') {
            if (last_char != HERMIT_PATH_SEPARATOR_CHAR) {
                *dst++ = HERMIT_PATH_SEPARATOR_CHAR;
                last_char = HERMIT_PATH_SEPARATOR_CHAR;
            }
        } else {
            *dst++ = *src;
            last_char = *src;
        }
        src++;
    }
    
    *dst = '\0';
    
    // Remove trailing separator (except for root)
    if (strlen(path) > 1 && path[strlen(path) - 1] == HERMIT_PATH_SEPARATOR_CHAR) {
        path[strlen(path) - 1] = '\0';
    }
    
    return 0;
}

int hermit_platform_get_absolute_path(const char *relative_path, char *absolute_path, size_t absolute_path_size)
{
    char *resolved_path;
    
    if (!relative_path || !absolute_path) {
        return -1;
    }
    
    resolved_path = realpath(relative_path, NULL);
    if (!resolved_path) {
        return -1;
    }
    
    strncpy(absolute_path, resolved_path, absolute_path_size - 1);
    absolute_path[absolute_path_size - 1] = '\0';
    
    free(resolved_path);
    return 0;
}

int hermit_platform_get_temp_path(char *temp_path, size_t temp_path_size)
{
    const char *tmp_dir;
    
    if (!temp_path) {
        return -1;
    }
    
    tmp_dir = getenv("TMPDIR");
    if (!tmp_dir) {
        tmp_dir = getenv("TEMP");
    }
    if (!tmp_dir) {
        tmp_dir = getenv("TMP");
    }
    if (!tmp_dir) {
        tmp_dir = "/tmp";
    }
    
    strncpy(temp_path, tmp_dir, temp_path_size - 1);
    temp_path[temp_path_size - 1] = '\0';
    
    return 0;
}

int hermit_platform_get_home_path(char *home_path, size_t home_path_size)
{
    const char *home_dir;
    
    if (!home_path) {
        return -1;
    }
    
    home_dir = getenv("HOME");
    if (!home_dir) {
        home_dir = getenv("USERPROFILE"); // Windows
    }
    if (!home_dir) {
        return -1;
    }
    
    strncpy(home_path, home_dir, home_path_size - 1);
    home_path[home_path_size - 1] = '\0';
    
    return 0;
}

int hermit_platform_get_memory_info(size_t *total, size_t *available, size_t *free)
{
#ifdef HERMIT_PLATFORM_LINUX
    struct sysinfo si;
    
    if (sysinfo(&si) != 0) {
        return -1;
    }
    
    if (total) *total = si.totalram * si.mem_unit;
    if (available) *available = si.freeram * si.mem_unit;
    if (free) *free = si.freeram * si.mem_unit;
    
    return 0;
#else
    // TODO: Implement for other platforms
    return -1;
#endif
}

int hermit_platform_get_cpu_info(int *cores, double *load_average)
{
#ifdef HERMIT_PLATFORM_LINUX
    long nprocs;
    
    if (cores) {
        nprocs = sysconf(_SC_NPROCESSORS_ONLN);
        if (nprocs > 0) {
            *cores = (int)nprocs;
        } else {
            *cores = 1;
        }
    }
    
    if (load_average) {
        double load[3];
        if (getloadavg(load, 3) > 0) {
            *load_average = load[0];
        } else {
            *load_average = 0.0;
        }
    }
    
    return 0;
#else
    // TODO: Implement for other platforms
    return -1;
#endif
}

int hermit_platform_get_disk_info(const char *path, size_t *total, size_t *available, size_t *free)
{
#ifdef HERMIT_PLATFORM_LINUX
    struct statfs fs;
    
    if (!path || statfs(path, &fs) != 0) {
        return -1;
    }
    
    if (total) *total = fs.f_blocks * fs.f_frsize;
    if (available) *available = fs.f_bavail * fs.f_frsize;
    if (free) *free = fs.f_bfree * fs.f_frsize;
    
    return 0;
#else
    // TODO: Implement for other platforms
    return -1;
#endif
}

const char* hermit_platform_strerror(int error_code)
{
    return strerror(error_code);
}

int hermit_platform_get_last_error(void)
{
    return errno;
}
