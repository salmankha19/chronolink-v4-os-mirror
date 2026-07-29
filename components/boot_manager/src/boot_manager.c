#include "boot_manager.h"

static boot_stage_t s_last_stage = BOOT_STAGE_NONE;

int boot_manager_init(boot_flags_t flags)
{
    (void)flags;
    s_last_stage = BOOT_STAGE_HANDOFF;
    return 0;
}

boot_stage_t boot_manager_get_stage(void)
{
    return s_last_stage;
}

void boot_manager_notify_stage(boot_stage_t stage, boot_status_t status)
{
    (void)status;
    s_last_stage = stage;
}
