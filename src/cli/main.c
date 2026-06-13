#include "hermit/cli/commands.h"
#include "hermit/common/error.h"
#include "hermit/common/fs.h"
#include "hermit/common/log.h"

int main(int argc, char **argv)
{
    hermit_app_context app;

    hermit_log_set_level(HERMIT_LOG_INFO);

    if (hermit_init_data_root(&app) != 0) {
        hermit_die_errno("hermit_init_data_root");
    }

    return hermit_cli_dispatch(&app, argc, argv);
}
