#include "boot_manager.h"
#include "hal.h"
#include "core_os.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "boot_manager";
static boot_stage_t s_last_stage = BOOT_STAGE_NONE;

/* --------------------------------------------------------------------------
 * Event publishing
 *
 * IMPORTANT ORDERING CONSTRAINT: event_bus_publish() is a no-op until
 * event_bus_init() has run, and event_bus_init() is called from
 * core_os_init(). Any code path in this file that runs BEFORE
 * core_os_init() cannot publish events -- the bus has no queue yet and
 * event_bus_publish() will log "no queue available, event dropped" and
 * return. That is why:
 *
 *   - EVENT_BOOT_START is not published from boot_manager_init(). It
 *     would need to fire before HAL_Init(), i.e. before the bus exists.
 *   - The HW stage publishes only OK, not START, for the same reason.
 *
 * Everything after core_os_init() publishes normally. If you ever want
 * pre-core-init boot events, event_bus_init() has to move out of
 * core_os_init() and into here (or into a boot-manager-owned early
 * init), which is a larger change than this file should make on its own.
 * -------------------------------------------------------------------------- */

static void publish_stage(boot_stage_t stage, boot_status_t status, int32_t err)
{
    event_t evt;
    boot_event_payload_u p;
    boot_event_init_stage(&p, stage, status, err);
    event_init_payload(&evt, EVENT_BOOT_STAGE, &p, sizeof(p));
    event_bus_publish(&evt);

    ESP_LOGI(TAG, "stage %s: %s (err=%ld)",
             boot_stage_to_str(stage), boot_status_to_str(status), (long)err);
}

static void publish_ready(void)
{
    event_t evt;
    boot_event_payload_u p;
    /* READY carries no interesting payload beyond "we got here"; stamp
       it with a health snapshot so the consumer doesn't have to special-
       case a zero-length event. */
    boot_event_init_health(&p, BOOT_STAGE_READY, 0,
                           (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                           0);
    event_init_payload(&evt, EVENT_BOOT_READY, &p, sizeof(p));
    event_bus_publish(&evt);

    ESP_LOGI(TAG, "boot ready");
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int boot_manager_init(boot_flags_t flags)
{
    ESP_LOGI(TAG, "Boot manager init (flags=0x%lx)", (unsigned long)flags);

    s_last_stage = BOOT_STAGE_INIT;

    /* HW stage: cannot publish START (bus not up yet); publishes OK below
       once core_os_init() has created the event bus. */
    s_last_stage = BOOT_STAGE_HW;
    if (HAL_Init() != HAL_OK) {
        /* Bus not up -- cannot publish EVENT_BOOT_FAIL here. See the
           ordering-constraint comment at the top of this file. */
        ESP_LOGE(TAG, "HAL_Init failed");
        return -1;
    }

    /* core_os_init() creates the event bus and the state queue. Every
       publish below this point is real. */
    s_last_stage = BOOT_STAGE_CORE_INIT;
    core_os_init(flags);

    publish_stage(BOOT_STAGE_HW,         BOOT_STATUS_OK, 0);
    publish_stage(BOOT_STAGE_CORE_INIT,  BOOT_STATUS_START, 0);

    s_last_stage = BOOT_STAGE_CORE_START;
    publish_stage(BOOT_STAGE_CORE_INIT,  BOOT_STATUS_OK, 0);
    publish_stage(BOOT_STAGE_CORE_START, BOOT_STATUS_START, 0);

    core_os_start();

    publish_stage(BOOT_STAGE_CORE_START, BOOT_STATUS_OK, 0);

    s_last_stage = BOOT_STAGE_HANDOFF;
    publish_stage(BOOT_STAGE_HANDOFF, BOOT_STATUS_OK, 0);

    s_last_stage = BOOT_STAGE_READY;
    publish_ready();

    ESP_LOGI(TAG, "Boot manager handoff complete");
    return 0;
}

int boot_manager_start(void) { return 0; }

boot_stage_t boot_manager_get_stage(void)
{
    return s_last_stage;
}

void boot_manager_notify_stage(boot_stage_t stage, boot_status_t status)
{
    s_last_stage = stage;
    publish_stage(stage, status, 0);
}