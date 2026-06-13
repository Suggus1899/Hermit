#define _GNU_SOURCE
#include "hermit/compose.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

static char* trim_string(char *str)
{
    char *end;
    
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == '\0') return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

static int parse_service_ports(yaml_node_t *ports_node, struct hermit_service *service)
{
    if (!ports_node || ports_node->type != YAML_SEQUENCE_NODE) {
        return 0;
    }
    
    yaml_node_item_t *item = ports_node->data.sequence.items.start;
    for (int i = 0; i < ports_node->data.sequence.items.top - ports_node->data.sequence.items.start && i < HERMIT_MAX_PORTS; i++) {
        yaml_node_t *port_node = *item++;
        if (port_node->type == YAML_SCALAR_NODE) {
            char *port_str = (char *)port_node->data.scalar.value;
            
            // Parse "host:container" or just "container"
            char *colon = strchr(port_str, ':');
            if (colon) {
                *colon = '\0';
                strncpy(service->ports[service->port_count].host_port, port_str, 15);
                strncpy(service->ports[service->port_count].container_port, colon + 1, 15);
            } else {
                strncpy(service->ports[service->port_count].container_port, port_str, 15);
                strcpy(service->ports[service->port_count].host_port, "");
            }
            
            strcpy(service->ports[service->port_count].protocol, "tcp");
            service->port_count++;
        }
    }
    
    return 0;
}

static int parse_service_volumes(yaml_node_t *volumes_node, struct hermit_service *service)
{
    if (!volumes_node || volumes_node->type != YAML_SEQUENCE_NODE) {
        return 0;
    }
    
    yaml_node_item_t *item = volumes_node->data.sequence.items.start;
    for (int i = 0; i < volumes_node->data.sequence.items.top - volumes_node->data.sequence.items.start && i < HERMIT_MAX_VOLUMES; i++) {
        yaml_node_t *volume_node = *item++;
        if (volume_node->type == YAML_SCALAR_NODE) {
            char *volume_str = (char *)volume_node->data.scalar.value;
            
            // Parse "host:container:mode" or "container"
            char *first_colon = strchr(volume_str, ':');
            if (first_colon) {
                *first_colon = '\0';
                strncpy(service->volumes[service->volume_count].host_path, volume_str, PATH_MAX - 1);
                
                char *second_colon = strchr(first_colon + 1, ':');
                if (second_colon) {
                    *second_colon = '\0';
                    strncpy(service->volumes[service->volume_count].container_path, first_colon + 1, PATH_MAX - 1);
                    strncpy(service->volumes[service->volume_count].mode, second_colon + 1, 31);
                } else {
                    strncpy(service->volumes[service->volume_count].container_path, first_colon + 1, PATH_MAX - 1);
                    strcpy(service->volumes[service->volume_count].mode, "rw");
                }
            } else {
                strncpy(service->volumes[service->volume_count].container_path, volume_str, PATH_MAX - 1);
                strcpy(service->volumes[service->volume_count].host_path, "");
                strcpy(service->volumes[service->volume_count].mode, "rw");
            }
            
            service->volume_count++;
        }
    }
    
    return 0;
}

static int parse_service_environment(yaml_node_t *env_node, struct hermit_service *service)
{
    if (!env_node || env_node->type != YAML_MAPPING_NODE) {
        return 0;
    }
    
    yaml_node_pair_t *pair = env_node->data.mapping.pairs.start;
    for (int i = 0; i < env_node->data.mapping.pairs.top - env_node->data.mapping.pairs.start && i < HERMIT_MAX_ENV_VARS; i++) {
        yaml_node_t *key_node = pair->key;
        yaml_node_t *value_node = pair->value;
        
        if (key_node->type == YAML_SCALAR_NODE && value_node->type == YAML_SCALAR_NODE) {
            char env_var[512];
            snprintf(env_var, sizeof(env_var), "%s=%s", 
                     (char *)key_node->data.scalar.value,
                     (char *)value_node->data.scalar.value);
            
            service->env_vars[service->env_count] = strdup(env_var);
            if (service->env_vars[service->env_count]) {
                service->env_count++;
            }
        }
        
        pair++;
    }
    
    return 0;
}

static int parse_service_dependencies(yaml_node_t *deps_node, struct hermit_service *service)
{
    if (!deps_node || deps_node->type != YAML_SEQUENCE_NODE) {
        return 0;
    }
    
    yaml_node_item_t *item = deps_node->data.sequence.items.start;
    for (int i = 0; i < deps_node->data.sequence.items.top - deps_node->data.sequence.items.start && i < HERMIT_MAX_DEPENDENCIES; i++) {
        yaml_node_t *dep_node = *item++;
        if (dep_node->type == YAML_SCALAR_NODE) {
            strncpy(service->depends_on[service->dependency_count], 
                   (char *)dep_node->data.scalar.value, 
                   HERMIT_MAX_SERVICE_NAME - 1);
            service->dependency_count++;
        }
    }
    
    return 0;
}

static int parse_service(yaml_node_t *service_node, struct hermit_service *service)
{
    if (!service_node || service_node->type != YAML_MAPPING_NODE) {
        return -1;
    }
    
    yaml_node_pair_t *pair = service_node->data.mapping.pairs.start;
    for (int i = 0; i < service_node->data.mapping.pairs.top - service_node->data.mapping.pairs.start; i++) {
        yaml_node_t *key_node = pair->key;
        yaml_node_t *value_node = pair->value;
        
        if (key_node->type == YAML_SCALAR_NODE) {
            char *key = (char *)key_node->data.scalar.value;
            
            if (strcmp(key, "image") == 0 && value_node->type == YAML_SCALAR_NODE) {
                strncpy(service->image, (char *)value_node->data.scalar.value, HERMIT_MAX_IMAGE_NAME - 1);
            } else if (strcmp(key, "command") == 0 && value_node->type == YAML_SCALAR_NODE) {
                strncpy(service->command, (char *)value_node->data.scalar.value, HERMIT_MAX_COMMAND_LEN - 1);
            } else if (strcmp(key, "working_dir") == 0 && value_node->type == YAML_SCALAR_NODE) {
                strncpy(service->working_dir, (char *)value_node->data.scalar.value, PATH_MAX - 1);
            } else if (strcmp(key, "user") == 0 && value_node->type == YAML_SCALAR_NODE) {
                strncpy(service->user, (char *)value_node->data.scalar.value, 63);
            } else if (strcmp(key, "restart") == 0 && value_node->type == YAML_SCALAR_NODE) {
                service->restart_policy = (strcmp((char *)value_node->data.scalar.value, "always") == 0 ||
                                        strcmp((char *)value_node->data.scalar.value, "unless-stopped") == 0);
            } else if (strcmp(key, "ports") == 0) {
                parse_service_ports(value_node, service);
            } else if (strcmp(key, "volumes") == 0) {
                parse_service_volumes(value_node, service);
            } else if (strcmp(key, "environment") == 0) {
                parse_service_environment(value_node, service);
            } else if (strcmp(key, "depends_on") == 0) {
                parse_service_dependencies(value_node, service);
            } else if (strcmp(key, "healthcheck") == 0 && value_node->type == YAML_MAPPING_NODE) {
                yaml_node_pair_t *hc_pair = value_node->data.mapping.pairs.start;
                for (int j = 0; j < value_node->data.mapping.pairs.top - value_node->data.mapping.pairs.start; j++) {
                    yaml_node_t *hc_key = hc_pair->key;
                    yaml_node_t *hc_value = hc_pair->value;
                    
                    if (hc_key->type == YAML_SCALAR_NODE && hc_value->type == YAML_SCALAR_NODE) {
                        char *hc_key_str = (char *)hc_key->data.scalar.value;
                        
                        if (strcmp(hc_key_str, "test") == 0) {
                            strncpy(service->healthcheck_cmd, (char *)hc_value->data.scalar.value, 511);
                        } else if (strcmp(hc_key_str, "interval") == 0) {
                            service->healthcheck_interval = atoi((char *)hc_value->data.scalar.value);
                        } else if (strcmp(hc_key_str, "timeout") == 0) {
                            service->healthcheck_timeout = atoi((char *)hc_value->data.scalar.value);
                        } else if (strcmp(hc_key_str, "retries") == 0) {
                            service->healthcheck_retries = atoi((char *)hc_value->data.scalar.value);
                        }
                    }
                    hc_pair++;
                }
            }
        }
        
        pair++;
    }
    
    return 0;
}

int hermit_parse_compose_file(const char *compose_file, struct hermit_compose_file *compose)
{
    FILE *fp;
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root;
    
    if (!compose_file || !compose) {
        return -1;
    }
    
    memset(compose, 0, sizeof(*compose));
    
    fp = fopen(compose_file, "r");
    if (!fp) {
        hermit_log(HERMIT_LOG_ERROR, "compose", "cannot open compose file: %s", strerror(errno));
        return -1;
    }
    
    if (!yaml_parser_initialize(&parser)) {
        fclose(fp);
        return -1;
    }
    
    yaml_parser_set_input_file(&parser, fp);
    
    if (!yaml_parser_load(&parser, &document)) {
        hermit_log(HERMIT_LOG_ERROR, "compose", "parse error: %s", parser.problem);
        yaml_parser_delete(&parser);
        fclose(fp);
        return -1;
    }
    
    root = yaml_document_get_root_node(&document);
    if (!root || root->type != YAML_MAPPING_NODE) {
        hermit_log(HERMIT_LOG_ERROR, "compose", "invalid root node type");
        yaml_document_delete(&document);
        yaml_parser_delete(&parser);
        fclose(fp);
        return -1;
    }
    
    // Parse version
    yaml_node_pair_t *pair = root->data.mapping.pairs.start;
    for (int i = 0; i < root->data.mapping.pairs.top - root->data.mapping.pairs.start; i++) {
        yaml_node_t *key_node = pair->key;
        yaml_node_t *value_node = pair->value;
        
        if (key_node->type == YAML_SCALAR_NODE) {
            char *key = (char *)key_node->data.scalar.value;
            
            if (strcmp(key, "version") == 0 && value_node->type == YAML_SCALAR_NODE) {
                strncpy(compose->version, (char *)value_node->data.scalar.value, 15);
            } else if (strcmp(key, "services") == 0 && value_node->type == YAML_MAPPING_NODE) {
                yaml_node_pair_t *service_pair = value_node->data.mapping.pairs.start;
                for (int j = 0; j < value_node->data.mapping.pairs.top - value_node->data.mapping.pairs.start && j < HERMIT_MAX_SERVICES; j++) {
                    yaml_node_t *service_name_node = service_pair->key;
                    yaml_node_t *service_node = service_pair->value;
                    
                    if (service_name_node->type == YAML_SCALAR_NODE) {
                        strncpy(compose->services[compose->service_count].name, 
                               (char *)service_name_node->data.scalar.value, 
                               HERMIT_MAX_SERVICE_NAME - 1);
                        
                        if (parse_service(service_node, &compose->services[compose->service_count]) == 0) {
                            compose->service_count++;
                        }
                    }
                    
                    service_pair++;
                }
            }
        }
        
        pair++;
    }
    
    yaml_document_delete(&document);
    yaml_parser_delete(&parser);
    fclose(fp);
    
    hermit_log(HERMIT_LOG_INFO, "compose", "parsed %d services from %s", compose->service_count, compose_file);
    return 0;
}

int hermit_validate_compose_file(const struct hermit_compose_file *compose)
{
    if (!compose) {
        return -1;
    }
    
    // Validate version
    if (strlen(compose->version) == 0) {
        hermit_log(HERMIT_LOG_ERROR, "compose", "missing version");
        return -1;
    }
    
    // Validate services
    for (int i = 0; i < compose->service_count; i++) {
        struct hermit_service *service = &compose->services[i];
        
        if (strlen(service->image) == 0) {
            hermit_log(HERMIT_LOG_ERROR, "compose", "service %s missing image", service->name);
            return -1;
        }
        
        // Validate dependencies
        for (int j = 0; j < service->dependency_count; j++) {
            bool found = false;
            for (int k = 0; k < compose->service_count; k++) {
                if (strcmp(service->depends_on[j], compose->services[k].name) == 0) {
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                hermit_log(HERMIT_LOG_ERROR, "compose", "service %s depends on non-existent service %s", 
                           service->name, service->depends_on[j]);
                return -1;
            }
        }
    }
    
    hermit_log(HERMIT_LOG_INFO, "compose", "compose file validation passed");
    return 0;
}

int hermit_get_service_by_name(const struct hermit_compose_file *compose, const char *name, struct hermit_service **service)
{
    if (!compose || !name || !service) {
        return -1;
    }
    
    for (int i = 0; i < compose->service_count; i++) {
        if (strcmp(compose->services[i].name, name) == 0) {
            *service = &compose->services[i];
            return 0;
        }
    }
    
    return -1;
}

void hermit_free_compose_file(struct hermit_compose_file *compose)
{
    if (!compose) return;
    
    // Free environment variables
    for (int i = 0; i < compose->service_count; i++) {
        for (int j = 0; j < compose->services[i].env_count; j++) {
            free(compose->services[i].env_vars[j]);
        }
    }
    
    memset(compose, 0, sizeof(*compose));
}
