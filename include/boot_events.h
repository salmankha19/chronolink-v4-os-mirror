/* boot_events.h
 *
 * Canonical boot event IDs and payloads for ChronoLink V4.
 * Keep payloads fixed-size and POD so they fit inside event_t.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_STAGE_NONE = 0,
    BOOT_STAGE_NVS = 1,
    BOOT_STAGE_FATFS = 2,
    BOOT_STAGE_CORE_INIT = 3,
    BOOT_STAGE_CORE_START = 4,
    BOOT_STAGE_HANDOFF = 5,
    BOOT_STAGE_RECOVERY = 6,
} boot_stage_t;

typedef enum {
    BOOT_STATUS_START = 0,
    BOOT_STATUS_OK = 1,
    BOOT_STATUS_FAIL = 2,
} boot_status_t;

typedef struct {
    uint32_t uptime_ms;
    uint8_t current_stage;
    uint8_t reserved;
    uint16_t error_count;
} boot_health_payload_t;

typedef struct {
    uint8_t stage;
    uint8_t status;
    int32_t err;
} boot_stage_payload_t;

typedef struct {
    uint8_t stage;
    uint8_t reserved;
    int32_t err;
    uint32_t fail_flags;
} boot_fail_payload_t;

#define EVENT_BOOT_HEALTH  0xB001
#define EVENT_BOOT_STAGE   0xB002
#define EVENT_BOOT_FAIL    0xB003

typedef union {
    boot_health_payload_t health;
    boot_stage_payload_t stage;
    boot_fail_payload_t  fail;
    uint8_t raw[16];
} boot_event_payload_u;

static inline const char *boot_stage_to_str(boot_stage_t s) {
    switch (s) {
    case BOOT_STAGE_NVS: return "NVS";
    case BOOT_STAGE_FATFS: return "FATFS";
    case BOOT_STAGE_CORE_INIT: return "CORE_INIT";
    case BOOT_STAGE_CORE_START: return "CORE_START";
    case BOOT_STAGE_HANDOFF: return "HANDOFF";
    case BOOT_STAGE_RECOVERY: return "RECOVERY";
    default: return "NONE";
    }
}

static inline const char *boot_status_to_str(boot_status_t st) {
    switch (st) {
    case BOOT_STATUS_START: return "START";
    case BOOT_STATUS_OK: return "OK";
    case BOOT_STATUS_FAIL: return "FAIL";
    default: return "UNKNOWN";
    }
}

#ifdef __cplusplus
}
#endif
