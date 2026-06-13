#define _GNU_SOURCE
#include "hermit/orchestrator.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char* container_state_to_string(enum hermit_container_state state)
{
    switch (state) {
        case HERMIT_CONTAINER_CREATED: return "created";
        case HERMIT_CONTAINER_STARTING: return "starting";
        case HERMIT_CONTAINER_RUNNING: return "running";
        case HERMIT_CONTAINER_STOPPING: return "stopping";
        case HERMIT_CONTAINER_STOPPED: return "stopped";
        case HERMIT_CONTAINER_FAILED: return "failed";
        case HERMIT_CONTAINER_UNHEALTHY: return "unhealthy";
        default: return "unknown";
    }
}

static int generate_container_id(char *id_out, size_t id_size)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    time_t now = time(NULL);
    
    srand(now ^ getpid());
    
    for (int i = 0; i < 12; i++) {
        int index = rand() % (sizeof(charset) - 1);
        id_out[i] = charset[index];
    }
    id_out[12] = '\0';
    
    return 0;
}

static struct hermit_container_instance* find_container_by_service(struct hermit_orchestrator *orch, const char *service_name)
{
    for (int i = 0; i < orch->container_count; i++) {
        if (strcmp(orch->containers[i].service_name, service_name) == 0) {
            return &orch->containers[i];
        }
    }
    return NULL;
}

static int check_service_dependencies(struct hermit_orchestrator *orch, const char *service_name)
{
    struct hermit_service *service;
    if (hermit_get_service_by_name(&orch->compose, service_name, &service) != 0) {
        return -1;
    }
    
    for (int i = 0; i < service->dependency_count; i++) {
        struct hermit_container_instance *dep_container = find_container_by_service(orch, service->depends_on[i]);
        if (!dep_container || dep_container->state != HERMIT_CONTAINER_RUNNING) {
            return -1; // Dependency not running
        }
    }
    
    return 0;
}

static int execute_container_command(struct hermit_orchestrator *orch, struct hermit_service *service, struct hermit_container_instance *container)
{
    char cmd[4096];
    char *args[64];
    int arg_count = 0;
    
    // Build container command
    snprintf(cmd, sizeof(cmd), "./hermit run --name %s", container->id);
    args[arg_count++] = "./hermit";
    args[arg_count++] = "run";
    args[arg_count++] = "--name";
    args[arg_count++] = container->id;
    
    // Add image
    args[arg_count++] = service->image;
    
    // Add environment variables
    for (int i = 0; i < service->env_count; i++) {
        char env_arg[512];
        snprintf(env_arg, sizeof(env_arg), "--env=%s", service->env_vars[i]);
        args[arg_count++] = strdup(env_arg);
    }
    
    // Add working directory
    if (strlen(service->working_dir) > 0) {
        char workdir_arg[512];
        snprintf(workdir_arg, sizeof(workdir_arg), "--workdir=%s", service->working_dir);
        args[arg_count++] = strdup(workdir_arg);
    }
    
    // Add user
    if (strlen(service->user) > 0) {
        char user_arg[256];
        snprintf(user_arg, sizeof(user_arg), "--user=%s", service->user);
        args[arg_count++] = strdup(user_arg);
    }
    
    // Add command
    if (strlen(service->command) > 0) {
        args[arg_count++] = service->command;
    }
    
    args[arg_count] = NULL;
    
    // Fork and execute
    container->pid = fork();
    if (container->pid == 0) {
        // Child process
        // Redirect stdout and stderr to log file
        if (strlen(container->log_file) > 0) {
            int log_fd = open(container->log_file, O_CREAT | O_WRONLY | O_APPEND, 0644);
            if (log_fd != -1) {
                dup2(log_fd, STDOUT_FILENO);
                dup2(log_fd, STDERR_FILENO);
                close(log_fd);
            }
        }
        
        execvp(args[0], args);
        exit(127); // exec failed
    } else if (container->pid > 0) {
        // Parent process
        container->state = HERMIT_CONTAINER_STARTING;
        container->created_at = time(NULL);
        
        hermit_log(HERMIT_LOG_INFO, "orchestrator", "started container %s for service %s (pid=%d)", 
                   container->id, service->name, container->pid);
    } else {
        hermit_log(HERMIT_LOG_ERROR, "orchestrator", "fork failed: %s", strerror(errno));
        return -1;
    }
    
    // Clean up allocated strings
    for (int i = 5; i < arg_count; i++) {
        if (args[i] != service->command) {
            free(args[i]);
        }
    }
    
    return 0;
}

int hermit_orchestrator_init(struct hermit_orchestrator *orch, const char *project_name, const char *compose_file)
{
    if (!orch || !project_name || !compose_file) {
        return -1;
    }
    
    memset(orch, 0, sizeof(*orch));
    
    strncpy(orch->project_name, project_name, sizeof(orch->project_name) - 1);
    strncpy(orch->compose_file, compose_file, sizeof(orch->compose_file) - 1);
    
    // Create log directory
    snprintf(orch->log_dir, sizeof(orch->log_dir), "/tmp/hermitd-state/logs/%s", project_name);
    if (mkdir(orch->log_dir, 0755) != 0 && errno != EEXIST) {
        hermit_log(HERMIT_LOG_ERROR, "orchestrator", "cannot create log directory: %s", strerror(errno));
        return -1;
    }
    
    // Parse compose file
    if (hermit_parse_compose_file(compose_file, &orch->compose) != 0) {
        return -1;
    }
    
    // Validate compose file
    if (hermit_validate_compose_file(&orch->compose) != 0) {
        hermit_free_compose_file(&orch->compose);
        return -1;
    }
    
    // Set state file path
    snprintf(orch->state_file, sizeof(orch->state_file), "/tmp/hermitd-state/%s.state", project_name);
    
    hermit_log(HERMIT_LOG_INFO, "orchestrator", "initialized orchestrator for project %s", project_name);
    return 0;
}

int hermit_orchestrator_up(struct hermit_orchestrator *orch)
{
    if (!orch) {
        return -1;
    }
    
    orch->running = true;
    orch->last_reconcile = time(NULL);
    
    // Start services in dependency order
    for (int i = 0; i < orch->compose.service_count; i++) {
        struct hermit_service *service = &orch->compose.services[i];
        
        if (find_container_by_service(orch, service->name)) {
            continue; // Already running
        }
        
        // Check dependencies
        if (check_service_dependencies(orch, service->name) != 0) {
            hermit_log(HERMIT_LOG_INFO, "orchestrator", "skipping service %s (dependencies not ready)", service->name);
            continue;
        }
        
        // Create container instance
        if (orch->container_count < HERMIT_MAX_CONTAINERS) {
            struct hermit_container_instance *container = &orch->containers[orch->container_count];
            
            memset(container, 0, sizeof(*container));
            strncpy(container->service_name, service->name, sizeof(container->service_name) - 1);
            strncpy(container->image, service->image, sizeof(container->image) - 1);
            container->detached = service->detached;
            
            generate_container_id(container->id, sizeof(container->id));
            
            // Set log file path
            snprintf(container->log_file, sizeof(container->log_file), "%s/%s.log", 
                     orch->log_dir, service->name);
            
            // Execute container
            if (execute_container_command(orch, service, container) == 0) {
                orch->container_count++;
            }
        }
    }
    
    // Save state
    hermit_orchestrator_save_state(orch);
    
    hermit_log(HERMIT_LOG_INFO, "orchestrator", "orchestrator up completed for %s", orch->project_name);
    return 0;
}

int hermit_orchestrator_down(struct hermit_orchestrator *orch)
{
    if (!orch) {
        return -1;
    }
    
    orch->running = false;
    
    // Stop all containers
    for (int i = 0; i < orch->container_count; i++) {
        struct hermit_container_instance *container = &orch->containers[i];
        
        if (container->pid > 0) {
            hermit_log(HERMIT_LOG_INFO, "orchestrator", "stopping container %s (pid=%d)", 
                       container->id, container->pid);
            
            kill(container->pid, SIGTERM);
            
            // Wait for process to exit
            int status;
            waitpid(container->pid, &status, 0);
            
            container->state = HERMIT_CONTAINER_STOPPED;
        }
    }
    
    // Clear containers
    orch->container_count = 0;
    memset(orch->containers, 0, sizeof(orch->containers));
    
    // Remove state file
    unlink(orch->state_file);
    
    hermit_log(HERMIT_LOG_INFO, "orchestrator", "orchestrator down completed for %s", orch->project_name);
    return 0;
}

int hermit_orchestrator_ps(struct hermit_orchestrator *orch)
{
    if (!orch) {
        return -1;
    }
    
    printf("%-20s %-15s %-10s %-20s %-8s\n", 
           "SERVICE", "CONTAINER", "STATE", "IMAGE", "PID");
    printf("%-20s %-15s %-10s %-20s %-8s\n", 
           "--------", "---------", "-----", "-----", "---");
    
    for (int i = 0; i < orch->container_count; i++) {
        struct hermit_container_instance *container = &orch->containers[i];
        
        printf("%-20s %-15s %-10s %-20s %-8d\n",
               container->service_name,
               container->id,
               container_state_to_string(container->state),
               container->image,
               container->pid);
    }
    
    return 0;
}

int hermit_orchestrator_logs(struct hermit_orchestrator *orch, const char *service_name, bool follow)
{
    if (!orch) {
        return -1;
    }
    
    for (int i = 0; i < orch->container_count; i++) {
        struct hermit_container_instance *container = &orch->containers[i];
        
        if (service_name == NULL || strcmp(container->service_name, service_name) == 0) {
            if (strlen(container->log_file) > 0) {
                FILE *fp = fopen(container->log_file, "r");
                if (fp) {
                    char line[1024];
                    
                    if (follow) {
                        // Follow mode - seek to end and wait
                        fseek(fp, 0, SEEK_END);
                        while (true) {
                            if (fgets(line, sizeof(line), fp)) {
                                printf("[%s] %s", container->service_name, line);
                                fflush(stdout);
                            } else {
                                usleep(100000); // 100ms
                            }
                        }
                    } else {
                        // Normal mode - read all
                        while (fgets(line, sizeof(line), fp)) {
                            printf("[%s] %s", container->service_name, line);
                        }
                    }
                    
                    fclose(fp);
                }
            }
        }
    }
    
    return 0;
}

int hermit_orchestrator_reconcile(struct hermit_orchestrator *orch)
{
    if (!orch || !orch->running) {
        return 0;
    }
    
    time_t now = time(NULL);
    if (now - orch->last_reconcile < 5) {
        return 0; // Reconcile at most every 5 seconds
    }
    
    orch->last_reconcile = now;
    
    // Check container states
    for (int i = 0; i < orch->container_count; i++) {
        struct hermit_container_instance *container = &orch->containers[i];
        
        if (container->pid > 0) {
            int status;
            int result = waitpid(container->pid, &status, WNOHANG);
            
            if (result == 0) {
                // Process still running
                if (container->state == HERMIT_CONTAINER_STARTING) {
                    container->state = HERMIT_CONTAINER_RUNNING;
                    container->started_at = now;
                    hermit_log(HERMIT_LOG_INFO, "orchestrator", "container %s is running", container->id);
                }
                
                // Perform healthcheck
                hermit_orchestrator_healthcheck_service(orch, container->service_name);
                
            } else if (result == -1) {
                // Error checking process
                hermit_log(HERMIT_LOG_WARN, "orchestrator", "error checking container %s: %s", 
                           container->id, strerror(errno));
            } else {
                // Process exited
                if (WIFEXITED(status)) {
                    hermit_log(HERMIT_LOG_INFO, "orchestrator", "container %s exited with status %d", 
                               container->id, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    hermit_log(HERMIT_LOG_INFO, "orchestrator", "container %s killed by signal %d", 
                               container->id, WTERMSIG(status));
                }
                
                container->state = HERMIT_CONTAINER_STOPPED;
                container->pid = 0;
                
                // Handle restart policy
                struct hermit_service *service;
                if (hermit_get_service_by_name(&orch->compose, container->service_name, &service) == 0) {
                    if (service->restart_policy && container->restart_count < 3) {
                        hermit_log(HERMIT_LOG_INFO, "orchestrator", "restarting container %s", container->id);
                        container->restart_count++;
                        execute_container_command(orch, service, container);
                    }
                }
            }
        }
    }
    
    // Try to start pending services
    hermit_orchestrator_up(orch);
    
    return 0;
}

int hermit_orchestrator_healthcheck_service(struct hermit_orchestrator *orch, const char *service_name)
{
    struct hermit_service *service;
    struct hermit_container_instance *container;
    
    if (hermit_get_service_by_name(&orch->compose, service_name, &service) != 0) {
        return -1;
    }
    
    container = find_container_by_service(orch, service_name);
    if (!container || container->state != HERMIT_CONTAINER_RUNNING) {
        return -1;
    }
    
    if (strlen(service->healthcheck_cmd) == 0) {
        return 0; // No healthcheck configured
    }
    
    // Execute healthcheck command
    int status = system(service->healthcheck_cmd);
    
    if (status == 0) {
        if (container->state == HERMIT_CONTAINER_UNHEALTHY) {
            container->state = HERMIT_CONTAINER_RUNNING;
            container->healthcheck_failures = 0;
            hermit_log(HERMIT_LOG_INFO, "orchestrator", "service %s is healthy again", service_name);
        }
    } else {
        container->healthcheck_failures++;
        
        if (container->healthcheck_failures >= service->healthcheck_retries) {
            container->state = HERMIT_CONTAINER_UNHEALTHY;
            hermit_log(HERMIT_LOG_WARN, "orchestrator", "service %s is unhealthy", service_name);
        }
    }
    
    return 0;
}

int hermit_orchestrator_save_state(struct hermit_orchestrator *orch)
{
    FILE *fp;
    
    if (!orch) {
        return -1;
    }
    
    fp = fopen(orch->state_file, "w");
    if (!fp) {
        return -1;
    }
    
    fprintf(fp, "# Hermit Orchestrator State\n");
    fprintf(fp, "project=%s\n", orch->project_name);
    fprintf(fp, "running=%d\n", orch->running ? 1 : 0);
    fprintf(fp, "container_count=%d\n", orch->container_count);
    fprintf(fp, "last_reconcile=%ld\n", orch->last_reconcile);
    
    for (int i = 0; i < orch->container_count; i++) {
        struct hermit_container_instance *container = &orch->containers[i];
        fprintf(fp, "container_%d_id=%s\n", i, container->id);
        fprintf(fp, "container_%d_service=%s\n", i, container->service_name);
        fprintf(fp, "container_%d_image=%s\n", i, container->image);
        fprintf(fp, "container_%d_pid=%d\n", i, container->pid);
        fprintf(fp, "container_%d_state=%d\n", i, container->state);
        fprintf(fp, "container_%d_created_at=%ld\n", i, container->created_at);
        fprintf(fp, "container_%d_started_at=%ld\n", i, container->started_at);
    }
    
    fclose(fp);
    return 0;
}

int hermit_orchestrator_load_state(struct hermit_orchestrator *orch)
{
    // TODO: Implement state loading
    (void)orch;
    return 0;
}

int hermit_orchestrator_cleanup(struct hermit_orchestrator *orch)
{
    if (!orch) {
        return 0;
    }
    
    // Stop all containers
    hermit_orchestrator_down(orch);
    
    // Free compose file
    hermit_free_compose_file(&orch->compose);
    
    memset(orch, 0, sizeof(*orch));
    
    hermit_log(HERMIT_LOG_INFO, "orchestrator", "orchestrator cleanup completed");
    return 0;
}
