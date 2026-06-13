#ifndef HERMIT_COMPOSE_H
#define HERMIT_COMPOSE_H

#include <stddef.h>
#include <stdbool.h>

#define HERMIT_MAX_SERVICES 32
#define HERMIT_MAX_SERVICE_NAME 64
#define HERMIT_MAX_IMAGE_NAME 256
#define HERMIT_MAX_COMMAND_LEN 1024
#define HERMIT_MAX_ENV_VARS 64
#define HERMIT_MAX_PORTS 16
#define HERMIT_MAX_VOLUMES 16
#define HERMIT_MAX_DEPENDENCIES 16

struct hermit_port_mapping {
    char host_port[16];
    char container_port[16];
    char protocol[8]; // tcp, udp
};

struct hermit_volume_mount {
    char host_path[PATH_MAX];
    char container_path[PATH_MAX];
    char mode[32]; // ro, rw, etc.
};

struct hermit_service {
    char name[HERMIT_MAX_SERVICE_NAME];
    char image[HERMIT_MAX_IMAGE_NAME];
    char command[HERMIT_MAX_COMMAND_LEN];
    char *env_vars[HERMIT_MAX_ENV_VARS];
    int env_count;
    struct hermit_port_mapping ports[HERMIT_MAX_PORTS];
    int port_count;
    struct hermit_volume_mount volumes[HERMIT_MAX_VOLUMES];
    int volume_count;
    char depends_on[HERMIT_MAX_DEPENDENCIES][HERMIT_MAX_SERVICE_NAME];
    int dependency_count;
    char working_dir[PATH_MAX];
    char user[64];
    bool restart_policy;
    char healthcheck_cmd[512];
    int healthcheck_interval;
    int healthcheck_timeout;
    int healthcheck_retries;
    bool detached;
};

struct hermit_compose_file {
    char version[16];
    char project_name[128];
    struct hermit_service services[HERMIT_MAX_SERVICES];
    int service_count;
    char networks[16][64];
    int network_count;
    char volumes[16][64];
    int volume_count;
};

int hermit_parse_compose_file(const char *compose_file, struct hermit_compose_file *compose);
int hermit_validate_compose_file(const struct hermit_compose_file *compose);
int hermit_get_service_by_name(const struct hermit_compose_file *compose, const char *name, struct hermit_service **service);
void hermit_free_compose_file(struct hermit_compose_file *compose);

#endif
