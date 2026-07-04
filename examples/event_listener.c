#include "event_bus.h"
#include "boot_events.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static const char *TAG = "event_listener";

static void dump_boot_health(const boot_health_payload_t *health)
{
    ESP_LOGI(TAG, "BOOT_HEALTH uptime_ms=%lu stage=%u errors=%u",
             (unsigned long)health->uptime_ms,
             (unsigned)health->current_stage,
             (unsigned)health->error_count);
}

static void dump_boot_stage(const boot_stage_payload_t *stage)
{
    ESP_LOGI(TAG, "BOOT_STAGE stage=%u status=%u err=%ld",
             (unsigned)stage->stage,
             (unsigned)stage->status,
             (long)stage->err);
}

static void dump_boot_fail(const boot_fail_payload_t *fail)
{
    ESP_LOGI(TAG, "BOOT_FAIL stage=%u err=%ld flags=0x%08lx",
             (unsigned)fail->stage,
             (long)fail->err,
             (unsigned long)fail->fail_flags);
}

void event_listener_poll_once(void)
{
    event_t evt;
    boot_event_payload_u p;

    if (event_bus_receive(&evt, pdMS_TO_TICKS(2000))) {
        memset(&p, 0, sizeof(p));
        memcpy(&p, &evt.data, sizeof(p));

        switch (evt.type) {
        case EVENT_BOOT_HEALTH:
            dump_boot_health(&p.health);
            break;
        case EVENT_BOOT_STAGE:
            dump_boot_stage(&p.stage);
            break;
        case EVENT_BOOT_FAIL:
            dump_boot_fail(&p.fail);
            break;
        default:
            ESP_LOGD(TAG, "Other event type: 0x%08x", (unsigned)evt.type);
            break;
        }
    }
}