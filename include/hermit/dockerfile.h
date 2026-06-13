#ifndef HERMIT_DOCKERFILE_H
#define HERMIT_DOCKERFILE_H

#include <stddef.h>

#define HERMIT_MAX_DOCKERFILE_LINE 1024
#define HERMIT_MAX_INSTRUCTIONS 128
#define HERMIT_MAX_ARGS 16

enum hermit_instruction_type {
    HERMIT_INSTRUCTION_FROM = 1,
    HERMIT_INSTRUCTION_WORKDIR,
    HERMIT_INSTRUCTION_COPY,
    HERMIT_INSTRUCTION_RUN,
    HERMIT_INSTRUCTION_ENV,
    HERMIT_INSTRUCTION_CMD,
    HERMIT_INSTRUCTION_ENTRYPOINT,
    HERMIT_INSTRUCTION_LABEL,
    HERMIT_INSTRUCTION_EXPOSE,
    HERMIT_INSTRUCTION_USER,
    HERMIT_INSTRUCTION_UNKNOWN
};

struct hermit_instruction {
    enum hermit_instruction_type type;
    char *args[HERMIT_MAX_ARGS];
    int arg_count;
    char raw_line[HERMIT_MAX_DOCKERFILE_LINE];
    int line_number;
};

struct hermit_dockerfile {
    struct hermit_instruction instructions[HERMIT_MAX_INSTRUCTIONS];
    int instruction_count;
    char base_image[256];
    char workdir[PATH_MAX];
    char *env_vars[32];
    int env_count;
    char *cmd[16];
    int cmd_count;
    char *entrypoint[16];
    int entrypoint_count;
    char user[64];
};

int hermit_parse_dockerfile(const char *dockerfile_path, struct hermit_dockerfile *dockerfile);
int hermit_validate_dockerfile(const struct hermit_dockerfile *dockerfile);
void hermit_free_dockerfile(struct hermit_dockerfile *dockerfile);
const char* hermit_instruction_type_to_string(enum hermit_instruction_type type);

#endif
