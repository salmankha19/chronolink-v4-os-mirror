#ifndef BOOT_MANAGER_H
#define BOOT_MANAGER_H

#include "event_bus/boot_events.h"   /* centralized boot enums/types */

/* Boot manager public API */
#ifdef __cplusplus
extern "C" {
#endif

void boot_manager_start(void);
void boot_manager_signal_stage(boot_stage_t stage, boot_status_t status);
void boot_manager_signal_fail(boot_stage_t stage, int32_t err, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_MANAGER_H */
