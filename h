[1mdiff --git a/components/boot_manager/include/boot_manager.h b/components/boot_manager/include/boot_manager.h[m
[1mindex b8c3112..3611a24 100644[m
[1m--- a/components/boot_manager/include/boot_manager.h[m
[1m+++ b/components/boot_manager/include/boot_manager.h[m
[36m@@ -1,44 +1,19 @@[m
[31m-#pragma once[m
[31m-#include <stdbool.h>[m
[32m+[m[32m#ifndef BOOT_MANAGER_H[m
[32m+[m[32m#define BOOT_MANAGER_H[m
 [m
[32m+[m[32m#include "boot_events.h"   /* centralized boot enums/types */[m
[32m+[m
[32m+[m[32m/* Boot manager public API */[m
 #ifdef __cplusplus[m
 extern "C" {[m
 #endif[m
 [m
[31m-/* Boot flags exposed to other components */[m
[31m-typedef struct {[m
[31m-    bool safe_mode;[m
[31m-    bool factory_reset;[m
[31m-    bool ota_allowed;[m
[31m-} boot_flags_t;[m
[31m-[m
[31m-/* Boot stages */[m
[31m-typedef enum {[m
[31m-    BOOT_STAGE_NONE = 0,[m
[31m-    BOOT_STAGE_NVS,[m
[31m-    BOOT_STAGE_HAL_INIT,[m
[31m-    BOOT_STAGE_FATFS,[m
[31m-    BOOT_STAGE_CORE_INIT,[m
[31m-    BOOT_STAGE_CORE_START,[m
[31m-    BOOT_STAGE_HANDOFF,[m
[31m-    BOOT_STAGE_RECOVERY,[m
[31m-} boot_stage_t;[m
[31m-[m
[31m-/* Boot status for stage events */[m
[31m-typedef enum {[m
[31m-    BOOT_STATUS_START = 0,[m
[31m-    BOOT_STATUS_OK,[m
[31m-    BOOT_STATUS_FAIL,[m
[31m-} boot_status_t;[m
[31m-[m
[31m-/* Public API */[m
[31m-bool boot_manager_init(void);[m
[31m-bool cl_fs_mount(void);[m
[31m-[m
[31m-/* Query boot state */[m
[31m-const boot_flags_t *boot_get_flags(void);[m
[31m-boot_stage_t boot_get_last_stage(void);[m
[32m+[m[32mvoid boot_manager_start(void);[m
[32m+[m[32mvoid boot_manager_signal_stage(boot_stage_t stage, boot_status_t status);[m
[32m+[m[32mvoid boot_manager_signal_fail(boot_stage_t stage, int32_t err, uint32_t flags);[m
 [m
 #ifdef __cplusplus[m
 }[m
 #endif[m
[32m+[m
[32m+[m[32m#endif /* BOOT_MANAGER_H */[m
