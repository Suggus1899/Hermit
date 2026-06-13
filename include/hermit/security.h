#ifndef HERMIT_SECURITY_H
#define HERMIT_SECURITY_H

#include <stddef.h>
#include <stdbool.h>

#define HERMIT_MAX_CAPABILITIES 64
#define HERMIT_MAX_SECCOMP_RULES 1024

struct hermit_security_profile {
    char name[64];
    bool no_new_privileges;
    bool read_only_rootfs;
    bool read_only_sys;
    char *capabilities[HERMIT_MAX_CAPABILITIES];
    int capability_count;
    char *seccomp_rules[HERMIT_MAX_SECCOMP_RULES];
    int seccomp_rule_count;
    char *apparmor_profile;
    char *selinux_context;
    int user_id;
    int group_id;
    bool drop_all_caps;
};

struct hermit_security_context {
    struct hermit_security_profile profile;
    char working_dir[PATH_MAX];
    char *env_vars[32];
    int env_count;
    char *mount_options[16];
    int mount_option_count;
};

int hermit_security_init_default(struct hermit_security_profile *profile);
int hermit_security_apply_profile(const struct hermit_security_profile *profile, pid_t pid);
int hermit_security_create_seccomp_filter(const struct hermit_security_profile *profile);
int hermit_security_drop_capabilities(const struct hermit_security_profile *profile);
int hermit_security_set_apparmor_profile(const char *profile_name);
int hermit_security_set_selinux_context(const char *context);
int hermit_security_validate_profile(const struct hermit_security_profile *profile);

#endif
