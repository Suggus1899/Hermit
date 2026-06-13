#ifndef HERMIT_CLI_COMMANDS_H
#define HERMIT_CLI_COMMANDS_H

#include "hermit/common/fs.h"

void hermit_cli_print_top_usage(const char *prog);
int hermit_cli_dispatch(hermit_app_context *ctx, int argc, char **argv);

#endif
