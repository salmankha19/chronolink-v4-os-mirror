#pragma once

#include "boot_events.h"

#ifdef __cplusplus
extern "C" {
#endif

int boot_manager_init(boot_flags_t flags);
int boot_manager_start(void);
boot_stage_t boot_manager_get_stage(void);
void boot_manager_notify_stage(boot_stage_t stage, boot_status_t status);

#ifdef __cplusplus
}
#endif
