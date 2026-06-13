#define _GNU_SOURCE
#include "hermit/dockerfile.h"

#include "hermit/common/error.h"
#include "hermit/common/log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enum hermit_instruction_type parse_instruction_type(const char *instruction)
{
    if (strcasecmp(instruction, "FROM") == 0) return HERMIT_INSTRUCTION_FROM;
    if (strcasecmp(instruction, "WORKDIR") == 0) return HERMIT_INSTRUCTION_WORKDIR;
    if (strcasecmp(instruction, "COPY") == 0) return HERMIT_INSTRUCTION_COPY;
    if (strcasecmp(instruction, "RUN") == 0) return HERMIT_INSTRUCTION_RUN;
    if (strcasecmp(instruction, "ENV") == 0) return HERMIT_INSTRUCTION_ENV;
    if (strcasecmp(instruction, "CMD") == 0) return HERMIT_INSTRUCTION_CMD;
    if (strcasecmp(instruction, "ENTRYPOINT") == 0) return HERMIT_INSTRUCTION_ENTRYPOINT;
    if (strcasecmp(instruction, "LABEL") == 0) return HERMIT_INSTRUCTION_LABEL;
    if (strcasecmp(instruction, "EXPOSE") == 0) return HERMIT_INSTRUCTION_EXPOSE;
    if (strcasecmp(instruction, "USER") == 0) return HERMIT_INSTRUCTION_USER;
    return HERMIT_INSTRUCTION_UNKNOWN;
}

static char* trim_whitespace(char *str)
{
    char *end;
    
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

static int parse_instruction_args(const char *line, char **args, int *arg_count)
{
    char *line_copy = strdup(line);
    char *token;
    int count = 0;
    
    if (!line_copy) return -1;
    
    token = strtok(line_copy, " \t");
    while (token != NULL && count < HERMIT_MAX_ARGS) {
        args[count] = strdup(token);
        if (!args[count]) {
            free(line_copy);
            return -1;
        }
        count++;
        token = strtok(NULL, " \t");
    }
    
    *arg_count = count;
    free(line_copy);
    return 0;
}

int hermit_parse_dockerfile(const char *dockerfile_path, struct hermit_dockerfile *dockerfile)
{
    FILE *fp;
    char line[HERMIT_MAX_DOCKERFILE_LINE];
    int line_number = 0;
    
    if (!dockerfile_path || !dockerfile) {
        return -1;
    }
    
    memset(dockerfile, 0, sizeof(*dockerfile));
    
    fp = fopen(dockerfile_path, "r");
    if (!fp) {
        hermit_log(HERMIT_LOG_ERROR, "dockerfile", "cannot open %s: %s", 
                   dockerfile_path, strerror(errno));
        return -1;
    }
    
    while (fgets(line, sizeof(line), fp) != NULL && dockerfile->instruction_count < HERMIT_MAX_INSTRUCTIONS) {
        line_number++;
        
        // Remove newline and trim
        line[strcspn(line, "\r\n")] = '\0';
        char *trimmed = trim_whitespace(line);
        
        // Skip empty lines and comments
        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }
        
        struct hermit_instruction *instr = &dockerfile->instructions[dockerfile->instruction_count];
        instr->line_number = line_number;
        strncpy(instr->raw_line, trimmed, sizeof(instr->raw_line) - 1);
        instr->raw_line[sizeof(instr->raw_line) - 1] = '\0';
        
        // Parse instruction type and args
        char *space = strchr(trimmed, ' ');
        if (space) {
            *space = '\0';
            instr->type = parse_instruction_type(trimmed);
            parse_instruction_args(space + 1, instr->args, &instr->arg_count);
        } else {
            instr->type = parse_instruction_type(trimmed);
            instr->arg_count = 0;
        }
        
        // Update dockerfile state based on instruction
        switch (instr->type) {
            case HERMIT_INSTRUCTION_FROM:
                if (instr->arg_count > 0) {
                    strncpy(dockerfile->base_image, instr->args[0], sizeof(dockerfile->base_image) - 1);
                    dockerfile->base_image[sizeof(dockerfile->base_image) - 1] = '\0';
                }
                break;
                
            case HERMIT_INSTRUCTION_WORKDIR:
                if (instr->arg_count > 0) {
                    strncpy(dockerfile->workdir, instr->args[0], sizeof(dockerfile->workdir) - 1);
                    dockerfile->workdir[sizeof(dockerfile->workdir) - 1] = '\0';
                }
                break;
                
            case HERMIT_INSTRUCTION_ENV:
                if (instr->arg_count >= 2 && dockerfile->env_count < 31) {
                    char env_var[512];
                    snprintf(env_var, sizeof(env_var), "%s=%s", instr->args[0], instr->args[1]);
                    dockerfile->env_vars[dockerfile->env_count] = strdup(env_var);
                    dockerfile->env_count++;
                }
                break;
                
            case HERMIT_INSTRUCTION_CMD:
                for (int i = 0; i < instr->arg_count && i < 15; i++) {
                    dockerfile->cmd[dockerfile->cmd_count] = strdup(instr->args[i]);
                    dockerfile->cmd_count++;
                }
                break;
                
            case HERMIT_INSTRUCTION_ENTRYPOINT:
                for (int i = 0; i < instr->arg_count && i < 15; i++) {
                    dockerfile->entrypoint[dockerfile->entrypoint_count] = strdup(instr->args[i]);
                    dockerfile->entrypoint_count++;
                }
                break;
                
            case HERMIT_INSTRUCTION_USER:
                if (instr->arg_count > 0) {
                    strncpy(dockerfile->user, instr->args[0], sizeof(dockerfile->user) - 1);
                    dockerfile->user[sizeof(dockerfile->user) - 1] = '\0';
                }
                break;
                
            default:
                break;
        }
        
        dockerfile->instruction_count++;
    }
    
    fclose(fp);
    hermit_log(HERMIT_LOG_INFO, "dockerfile", "parsed %d instructions from %s", 
               dockerfile->instruction_count, dockerfile_path);
    
    return 0;
}

int hermit_validate_dockerfile(const struct hermit_dockerfile *dockerfile)
{
    if (!dockerfile) {
        return -1;
    }
    
    // Must have FROM instruction
    int has_from = 0;
    for (int i = 0; i < dockerfile->instruction_count; i++) {
        if (dockerfile->instructions[i].type == HERMIT_INSTRUCTION_FROM) {
            has_from = 1;
            break;
        }
    }
    
    if (!has_from) {
        hermit_log(HERMIT_LOG_ERROR, "dockerfile", "missing FROM instruction");
        return -1;
    }
    
    // Validate base image
    if (strlen(dockerfile->base_image) == 0) {
        hermit_log(HERMIT_LOG_ERROR, "dockerfile", "empty base image");
        return -1;
    }
    
    // Validate workdir if specified
    if (strlen(dockerfile->workdir) > 0 && dockerfile->workdir[0] != '/') {
        hermit_log(HERMIT_LOG_ERROR, "dockerfile", "WORKDIR must be absolute path");
        return -1;
    }
    
    hermit_log(HERMIT_LOG_INFO, "dockerfile", "dockerfile validation passed");
    return 0;
}

void hermit_free_dockerfile(struct hermit_dockerfile *dockerfile)
{
    if (!dockerfile) return;
    
    // Free instruction args
    for (int i = 0; i < dockerfile->instruction_count; i++) {
        for (int j = 0; j < dockerfile->instructions[i].arg_count; j++) {
            free(dockerfile->instructions[i].args[j]);
        }
    }
    
    // Free env vars
    for (int i = 0; i < dockerfile->env_count; i++) {
        free(dockerfile->env_vars[i]);
    }
    
    // Free cmd
    for (int i = 0; i < dockerfile->cmd_count; i++) {
        free(dockerfile->cmd[i]);
    }
    
    // Free entrypoint
    for (int i = 0; i < dockerfile->entrypoint_count; i++) {
        free(dockerfile->entrypoint[i]);
    }
    
    memset(dockerfile, 0, sizeof(*dockerfile));
}

const char* hermit_instruction_type_to_string(enum hermit_instruction_type type)
{
    switch (type) {
        case HERMIT_INSTRUCTION_FROM: return "FROM";
        case HERMIT_INSTRUCTION_WORKDIR: return "WORKDIR";
        case HERMIT_INSTRUCTION_COPY: return "COPY";
        case HERMIT_INSTRUCTION_RUN: return "RUN";
        case HERMIT_INSTRUCTION_ENV: return "ENV";
        case HERMIT_INSTRUCTION_CMD: return "CMD";
        case HERMIT_INSTRUCTION_ENTRYPOINT: return "ENTRYPOINT";
        case HERMIT_INSTRUCTION_LABEL: return "LABEL";
        case HERMIT_INSTRUCTION_EXPOSE: return "EXPOSE";
        case HERMIT_INSTRUCTION_USER: return "USER";
        default: return "UNKNOWN";
    }
}
