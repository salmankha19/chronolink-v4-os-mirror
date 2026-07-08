#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot flags exposed to other components */
typedef struct {
    bool safe_mode;
    bool factory_reset;
    bool ota_allowed;
} boot_flags_t;

/* Boot stages */
typedef enum {
    BOOT_STAGE_NONE = 0,
    BOOT_STAGE_NVS,
    BOOT_STAGE_HAL_INIT,
    BOOT_STAGE_FATFS,
    BOOT_STAGE_CORE_INIT,
    BOOT_STAGE_CORE_START,
    BOOT_STAGE_HANDOFF,
    BOOT_STAGE_RECOVERY,
} boot_stage_t;

/* Boot status for stage events */
typedef enum {
    BOOT_STATUS_START = 0,
    BOOT_STATUS_OK,
    BOOT_STATUS_FAIL,
} boot_status_t;

/* Public API */
bool boot_manager_init(void);
bool cl_fs_mount(void);

/* Query boot state */
const boot_flags_t *boot_get_flags(void);
boot_stage_t boot_get_last_stage(void);

#ifdef __cplusplus
}
#endif
