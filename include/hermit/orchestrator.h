#ifndef HERMIT_ORCHESTRATOR_H
#define HERMIT_ORCHESTRATOR_H

#include "hermit/compose.h"
#include <stddef.h>
#include <stdbool.h>

#define HERMIT_MAX_CONTAINERS 64
#define HERMIT_MAX_EVENTS 1024

enum hermit_container_state {
    HERMIT_CONTAINER_CREATED = 1,
    HERMIT_CONTAINER_STARTING,
    HERMIT_CONTAINER_RUNNING,
    HERMIT_CONTAINER_STOPPING,
    HERMIT_CONTAINER_STOPPED,
    HERMIT_CONTAINER_FAILED,
    HERMIT_CONTAINER_UNHEALTHY
};

struct hermit_container_instance {
    char id[64];
    char service_name[HERMIT_MAX_SERVICE_NAME];
    char image[HERMIT_MAX_IMAGE_NAME];
    pid_t pid;
    enum hermit_container_state state;
    time_t created_at;
    time_t started_at;
    int healthcheck_failures;
    int restart_count;
    char log_file[PATH_MAX];
    bool detached;
};

struct hermit_orchestrator {
    char project_name[128];
    char compose_file[PATH_MAX];
    struct hermit_compose_file compose;
    struct hermit_container_instance containers[HERMIT_MAX_CONTAINERS];
    int container_count;
    bool running;
    time_t last_reconcile;
    char state_file[PATH_MAX];
    char log_dir[PATH_MAX];
};

int hermit_orchestrator_init(struct hermit_orchestrator *orch, const char *project_name, const char *compose_file);
int hermit_orchestrator_up(struct hermit_orchestrator *orch);
int hermit_orchestrator_down(struct hermit_orchestrator *orch);
int hermit_orchestrator_ps(struct hermit_orchestrator *orch);
int hermit_orchestrator_logs(struct hermit_orchestrator *orch, const char *service_name, bool follow);
int hermit_orchestrator_reconcile(struct hermit_orchestrator *orch);
int hermit_orchestrator_start_service(struct hermit_orchestrator *orch, const char *service_name);
int hermit_orchestrator_stop_service(struct hermit_orchestrator *orch, const char *service_name);
int hermit_orchestrator_restart_service(struct hermit_orchestrator *orch, const char *service_name);
int hermit_orchestrator_healthcheck_service(struct hermit_orchestrator *orch, const char *service_name);
int hermit_orchestrator_save_state(struct hermit_orchestrator *orch);
int hermit_orchestrator_load_state(struct hermit_orchestrator *orch);
int hermit_orchestrator_cleanup(struct hermit_orchestrator *orch);

#endif
