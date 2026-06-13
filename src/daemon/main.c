#include "hermit/daemon/server.h"
#include "hermit/common/log.h"

int main(void)
{
    hermit_log_set_level(HERMIT_LOG_INFO);
    return hermitd_run();
}
