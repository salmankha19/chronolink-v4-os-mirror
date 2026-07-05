#ifndef EVENT_BUS_BOOT_EVENTS_H
#define EVENT_BUS_BOOT_EVENTS_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_STAGE_NONE = 0,
    BOOT_STAGE_INIT,
    BOOT_STAGE_HW,
    BOOT_STAGE_NVS,
    BOOT_STAGE_FATFS,
    BOOT_STAGE_CORE_INIT,
    BOOT_STAGE_CORE_START,
    BOOT_STAGE_HANDOFF,
    BOOT_STAGE_RECOVERY,
    BOOT_STAGE_SERVICES,
    BOOT_STAGE_READY
} boot_stage_t;

typedef enum {
    BOOT_STATUS_START = 0,
    BOOT_STATUS_OK,
    BOOT_STATUS_FAIL
} boot_status_t;

typedef struct {
    uint8_t current_stage;
    uint8_t last_error_code;
    uint32_t uptime_ms;
    uint16_t error_count;
} boot_health_t;

typedef struct {
    uint8_t stage;
    uint8_t status;
    int32_t err;
} boot_stage_payload_t;

typedef struct {
    uint8_t stage;
    int32_t err;
    uint32_t fail_flags;
} boot_fail_t;

typedef struct {
    uint32_t timestamp;
} boot_timestamp_t;

typedef union {
    boot_health_t health;
    boot_stage_payload_t stage;
    boot_fail_t   fail;
    boot_timestamp_t ts;
    uint8_t raw[32];
} boot_event_payload_u;

#define EVENT_BOOT_NONE        0
#define EVENT_BOOT_START       1
#define EVENT_BOOT_READY       2
#define EVENT_BOOT_ERROR       3
#define EVENT_BOOT_HEALTH      4
#define EVENT_BOOT_FAIL        5
#define EVENT_BOOT_STAGE       6

static inline void boot_event_init_health(boot_event_payload_u *p, boot_stage_t stage, uint8_t err, uint32_t uptime_ms, uint16_t error_count)
{
    memset(p, 0, sizeof(*p));
    p->health.current_stage = (uint8_t)stage;
    p->health.last_error_code = err;
    p->health.uptime_ms = uptime_ms;
    p->health.error_count = error_count;
}

static inline void boot_event_init_stage(boot_event_payload_u *p, boot_stage_t stage, boot_status_t status, int32_t err)
{
    memset(p, 0, sizeof(*p));
    p->stage.stage = (uint8_t)stage;
    p->stage.status = (uint8_t)status;
    p->stage.err = err;
}

static inline void boot_event_init_fail(boot_event_payload_u *p, boot_stage_t stage, int32_t err, uint32_t flags)
{
    memset(p, 0, sizeof(*p));
    p->fail.stage = (uint8_t)stage;
    p->fail.err = err;
    p->fail.fail_flags = flags;
}

const char *boot_stage_to_str(boot_stage_t stage);
const char *boot_status_to_str(boot_status_t status);

#ifdef __cplusplus
}
#endif

#endif // EVENT_BUS_BOOT_EVENTS_H