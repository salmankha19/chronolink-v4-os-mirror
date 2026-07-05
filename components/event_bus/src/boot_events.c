#include "boot_events.h"

const char *boot_stage_to_str(boot_stage_t stage)
{
    switch(stage) {
    case BOOT_STAGE_NONE: return "NONE";
    case BOOT_STAGE_INIT: return "INIT";
    case BOOT_STAGE_HW: return "HW";
    case BOOT_STAGE_NVS: return "NVS";
    case BOOT_STAGE_FATFS: return "FATFS";
    case BOOT_STAGE_CORE_INIT: return "CORE_INIT";
    case BOOT_STAGE_CORE_START: return "CORE_START";
    case BOOT_STAGE_HANDOFF: return "HANDOFF";
    case BOOT_STAGE_RECOVERY: return "RECOVERY";
    case BOOT_STAGE_SERVICES: return "SERVICES";
    case BOOT_STAGE_READY: return "READY";
    default: return "UNKNOWN";
    }
}

const char *boot_status_to_str(boot_status_t status)
{
    switch(status) {
    case BOOT_STATUS_START: return "START";
    case BOOT_STATUS_OK:    return "OK";
    case BOOT_STATUS_FAIL:  return "FAIL";
    default: return "UNKNOWN";
    }
}
