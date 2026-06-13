#define _GNU_SOURCE
#include "hermit/security.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <linux/seccomp.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* capability_names[] = {
    "CAP_CHOWN", "CAP_DAC_OVERRIDE", "CAP_DAC_READ_SEARCH", "CAP_FOWNER", "CAP_FSETID",
    "CAP_KILL", "CAP_SETGID", "CAP_SETUID", "CAP_SETPCAP", "CAP_LINUX_IMMUTABLE",
    "CAP_NET_BIND_SERVICE", "CAP_NET_BROADCAST", "CAP_NET_ADMIN", "CAP_NET_RAW",
    "CAP_IPC_LOCK", "CAP_IPC_OWNER", "CAP_SYS_MODULE", "CAP_SYS_RAWIO",
    "CAP_SYS_CHROOT", "CAP_SYS_PTRACE", "CAP_SYS_PACCT", "CAP_SYS_ADMIN",
    "CAP_SYS_BOOT", "CAP_SYS_NICE", "CAP_SYS_RESOURCE", "CAP_SYS_TIME",
    "CAP_SYS_TTY_CONFIG", "CAP_MKNOD", "CAP_LEASE", "CAP_AUDIT_WRITE",
    "CAP_AUDIT_CONTROL", "CAP_SETFCAP"
};

static int get_capability_value(const char *name)
{
    for (int i = 0; i < sizeof(capability_names) / sizeof(capability_names[0]); i++) {
        if (strcmp(name, capability_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

int hermit_security_init_default(struct hermit_security_profile *profile)
{
    if (!profile) {
        return -1;
    }
    
    memset(profile, 0, sizeof(*profile));
    
    // Default secure profile
    strcpy(profile->name, "default");
    profile->no_new_privileges = true;
    profile->read_only_rootfs = false;
    profile->read_only_sys = true;
    profile->drop_all_caps = true;
    profile->user_id = 65534; // nobody
    profile->group_id = 65534; // nogroup
    
    // Allow minimal capabilities for basic functionality
    profile->capabilities[profile->capability_count++] = strdup("CAP_SETUID");
    profile->capabilities[profile->capability_count++] = strdup("CAP_SETGID");
    profile->capabilities[profile->capability_count++] = strdup("CAP_CHOWN");
    
    // Basic seccomp rules
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("read");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("write");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("open");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("close");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("stat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fstat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("lstat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("poll");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("lseek");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mmap");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mprotect");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("munmap");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("brk");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rt_sigaction");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rt_sigprocmask");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rt_sigreturn");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("ioctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("pread64");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("pwrite64");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("readv");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("writev");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("access");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("pipe");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("select");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_yield");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mremap");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("msync");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mincore");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("madvise");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("shmget");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("shmat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("shmdt");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("shmctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("dup");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("dup2");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("pause");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("nanosleep");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getitimer");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("alarm");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setitimer");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getpid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sendfile");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("socket");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("connect");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("accept");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sendto");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("recvfrom");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sendmsg");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("recvmsg");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("shutdown");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("bind");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("listen");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getsockname");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getpeername");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("socketpair");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setsockopt");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getsockopt");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("clone");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fork");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("vfork");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("execve");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("exit");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("wait4");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("kill");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("uname");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("semget");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("semop");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("semctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("shmdt");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("msgget");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("msgsnd");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("msgrcv");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("msgctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fcntl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("flock");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fsync");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fdatasync");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("truncate");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("ftruncate");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getdents");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getcwd");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("chdir");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fchdir");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rename");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mkdir");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rmdir");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("creat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("link");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("unlink");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("symlink");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("readlink");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("chmod");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fchmod");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("chown");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fchown");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("lchown");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("umask");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("gettimeofday");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getrlimit");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getrusage");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sysinfo");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("times");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("ptrace");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getuid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("syslog");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getgid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setuid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setgid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("geteuid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getegid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setpgid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getppid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getpgrp");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setsid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setreuid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setregid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getgroups");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setgroups");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setresuid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setresgid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getresuid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getresgid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getpgid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setfsuid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setfsgid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getsid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("capget");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("capset");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rt_sigpending");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rt_sigtimedwait");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rt_sigqueueinfo");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rt_sigsuspend");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sigaltstack");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("utime");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mknod");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("uselib");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("personality");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("ustat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("statfs");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fstatfs");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sysfs");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getpriority");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setpriority");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_setparam");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_getparam");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_setscheduler");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_getscheduler");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_get_priority_max");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_get_priority_min");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_rr_get_interval");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mlock");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("munlock");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mlockall");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("munlockall");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("vhangup");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("modify_ldt");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("pivot_root");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("_sysctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("prctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("arch_prctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("adjtimex");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setrlimit");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("chroot");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sync");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("acct");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("settimeofday");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mount");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("umount2");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("swapon");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("swapoff");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("reboot");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sethostname");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setdomainname");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("iopl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("ioperm");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("create_module");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("init_module");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("delete_module");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("get_kernel_syms");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("query_module");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("quotactl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("nfsservctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getpmsg");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("putpmsg");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("afs_syscall");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("tuxcall");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("security");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("gettid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("readahead");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setxattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("lsetxattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fsetxattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getxattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("lgetxattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fgetxattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("listxattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("llistxattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("flistxattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("removexattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("lremovexattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fremovexattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("tkill");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("time");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("futex");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_setaffinity");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_getaffinity");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("set_thread_area");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("io_setup");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("io_destroy");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("io_getevents");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("io_submit");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("io_cancel");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("get_thread_area");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("lookup_dcookie");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("epoll_create");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("epoll_ctl_old");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("epoll_wait_old");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("remap_file_pages");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getdents64");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("set_tid_address");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("restart_syscall");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("semtimedop");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fadvise64");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("timer_create");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("timer_settime");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("timer_gettime");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("timer_getoverrun");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("timer_delete");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("clock_settime");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("clock_gettime");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("clock_getres");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("clock_nanosleep");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("exit_group");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("epoll_wait");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("epoll_ctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("tgkill");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("utimes");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("vserver");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mbind");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("set_mempolicy");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("get_mempolicy");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mq_open");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mq_unlink");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mq_timedsend");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mq_timedreceive");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mq_notify");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mq_getsetattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("kexec_load");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("waitid");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("add_key");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("request_key");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("keyctl");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("ioprio_set");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("ioprio_get");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("inotify_init");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("inotify_add_watch");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("inotify_rm_watch");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("migrate_pages");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("openat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mkdirat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("mknodat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fchownat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("futimesat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("newfstatat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("unlinkat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("renameat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("linkat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("symlinkat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("readlinkat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fchmodat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("faccessat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("pselect6");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("ppoll");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("unshare");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("set_robust_list");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("get_robust_list");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("splice");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("tee");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sync_file_range");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("vmsplice");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("move_pages");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("utimensat");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("epoll_pwait");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("signalfd");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("timerfd_create");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("eventfd");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fallocate");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("timerfd_settime");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("timerfd_gettime");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("accept4");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("signalfd");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("eventfd2");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("epoll_create1");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("dup3");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("pipe2");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("inotify_init1");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("preadv");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("pwritev");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("rt_tgsigqueueinfo");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("perf_event_open");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("recvmmsg");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("fanotify_init");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setrlimit");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("name_to_handle_at");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("open_by_handle_at");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("clock_adjtime");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("syncfs");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sendmmsg");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("setns");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getcpu");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("process_vm_readv");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("process_vm_writev");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("kcmp");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("finit_module");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_setattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("sched_getattr");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("renameat2");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("seccomp");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("getrandom");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("memfd_create");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("kexec_file_load");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("bpf");
    profile->seccomp_rules[profile->seccomp_rule_count++] = strdup("execveat");
    
    hermit_log(HERMIT_LOG_INFO, "security", "initialized default security profile");
    return 0;
}

int hermit_security_apply_profile(const struct hermit_security_profile *profile, pid_t pid)
{
    if (!profile || pid <= 0) {
        return -1;
    }
    
    // Apply no_new_privileges
    if (profile->no_new_privileges) {
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
            hermit_log(HERMIT_LOG_WARN, "security", "failed to set no_new_privileges: %s", strerror(errno));
        }
    }
    
    // Drop capabilities
    if (profile->drop_all_caps) {
        hermit_security_drop_capabilities(profile);
    }
    
    // Set user/group
    if (profile->user_id != 0) {
        if (setuid(profile->user_id) != 0) {
            hermit_log(HERMIT_LOG_WARN, "security", "failed to set uid %d: %s", 
                       profile->user_id, strerror(errno));
        }
    }
    
    if (profile->group_id != 0) {
        if (setgid(profile->group_id) != 0) {
            hermit_log(HERMIT_LOG_WARN, "security", "failed to set gid %d: %s", 
                       profile->group_id, strerror(errno));
        }
    }
    
    hermit_log(HERMIT_LOG_INFO, "security", "applied security profile %s to pid %d", 
               profile->name, pid);
    
    return 0;
}

int hermit_security_drop_capabilities(const struct hermit_security_profile *profile)
{
    cap_t caps;
    
    if (!profile) {
        return -1;
    }
    
    caps = cap_get_proc();
    if (!caps) {
        hermit_log(HERMIT_LOG_ERROR, "security", "failed to get process capabilities");
        return -1;
    }
    
    // Clear all capabilities
    cap_clear(caps);
    
    // Set specific capabilities if needed
    for (int i = 0; i < profile->capability_count; i++) {
        int cap_value = get_capability_value(profile->capabilities[i]);
        if (cap_value >= 0) {
            cap_set_flag(caps, cap_value, CAP_SET);
        }
    }
    
    // Apply capabilities
    if (cap_set_proc(caps) != 0) {
        hermit_log(HERMIT_LOG_ERROR, "security", "failed to set process capabilities");
        cap_free(caps);
        return -1;
    }
    
    cap_free(caps);
    hermit_log(HERMIT_LOG_INFO, "security", "dropped capabilities, kept %d specific caps", 
               profile->capability_count);
    
    return 0;
}

int hermit_security_create_seccomp_filter(const struct hermit_security_profile *profile)
{
    // TODO: Implement seccomp filter creation
    // This would use libseccomp to create a BPF filter
    hermit_log(HERMIT_LOG_INFO, "security", "seccomp filter creation not yet implemented");
    return 0;
}

int hermit_security_set_apparmor_profile(const char *profile_name)
{
    // TODO: Implement AppArmor profile setting
    hermit_log(HERMIT_LOG_INFO, "security", "AppArmor profile setting not yet implemented");
    return 0;
}

int hermit_security_set_selinux_context(const char *context)
{
    // TODO: Implement SELinux context setting
    hermit_log(HERMIT_LOG_INFO, "security", "SELinux context setting not yet implemented");
    return 0;
}

int hermit_security_validate_profile(const struct hermit_security_profile *profile)
{
    if (!profile) {
        return -1;
    }
    
    // Validate profile name
    if (strlen(profile->name) == 0) {
        hermit_log(HERMIT_LOG_ERROR, "security", "security profile name cannot be empty");
        return -1;
    }
    
    // Validate capabilities
    for (int i = 0; i < profile->capability_count; i++) {
        if (get_capability_value(profile->capabilities[i]) < 0) {
            hermit_log(HERMIT_LOG_ERROR, "security", "invalid capability: %s", 
                       profile->capabilities[i]);
            return -1;
        }
    }
    
    hermit_log(HERMIT_LOG_INFO, "security", "security profile validation passed");
    return 0;
}
