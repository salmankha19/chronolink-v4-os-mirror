#include "core_os.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "event_bus.h"
#include "state_manager.h"
#include "boot_events.h" 
#include <string.h>

static const char *TAG = "core_os";

/* Global handles for OS primitives */
static QueueHandle_t g_event_queue = NULL;
static QueueHandle_t g_state_queue = NULL;
static TimerHandle_t g_heartbeat_timer = NULL;

/* Cached boot flags */
static boot_flags_t g_boot_flags = 0;

/* Simple uptime helper (ms) */
static uint32_t get_uptime_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void heartbeat_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    ESP_LOGD(TAG, "Heartbeat timer tick");

    event_t evt;
    boot_event_payload_u p;
     memset(&evt, 0, sizeof(evt));
    memset(&p, 0, sizeof(p));

    p.health.uptime_ms = get_uptime_ms();
     p.health.current_stage = (uint8_t)BOOT_STAGE_NONE;
     p.health.error_count = 0;

     event_init_payload(&evt, EVENT_BOOT_HEALTH, &p, sizeof(p));
    event_bus_publish(&evt);

    if (g_state_queue) {
        os_state_msg_t msg = { .id = 0, .value = 0 };
        (void)xQueueSend(g_state_queue, &msg, 0);
    }
}

void core_os_init(boot_flags_t boot_flags)
{
    ESP_LOGI(TAG, "Core OS init start");

    /* 1. Cache boot flags */
    g_boot_flags = boot_flags;
    ESP_LOGI(TAG,
             "Boot flags: safe_mode=%d factory_reset=%d ota_allowed=%d",
             (g_boot_flags & BOOT_FLAG_SAFE_MODE) != 0,
             (g_boot_flags & BOOT_FLAG_FACTORY_RESET) != 0,
             (g_boot_flags & BOOT_FLAG_OTA_ALLOWED) != 0);

    /* 2. Create queues */
    g_event_queue = xQueueCreate(32, sizeof(event_t));
    g_state_queue = xQueueCreate(8, sizeof(os_state_msg_t));

    if (!g_event_queue || !g_state_queue) {
        ESP_LOGE(TAG, "Failed to create OS queues: event=%p state=%p", (void*)g_event_queue, (void*)g_state_queue);
        return;
    }

    /* Initialize Event Bus with the created queue */
    event_bus_init(g_event_queue);

    /* Initialize State Manager with the state queue */
    state_manager_init(g_state_queue);

    /* 3. Create timers */
    g_heartbeat_timer = xTimerCreate("heartbeat",
                                     pdMS_TO_TICKS(1000),
                                     pdTRUE,
                                     NULL,
                                     heartbeat_timer_cb);
    if (!g_heartbeat_timer) {
        ESP_LOGE(TAG, "Failed to create heartbeat timer");
        return;
    }

    if (xTimerStart(g_heartbeat_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start heartbeat timer");
        return;
    }

    /* 4. Safe mode handling (no services, minimal OS) */
    if (g_boot_flags & BOOT_FLAG_SAFE_MODE) {
        ESP_LOGW(TAG, "Safe mode active: core_os_start() will skip normal services");
    }

    ESP_LOGI(TAG, "Core OS init complete");
}

static void core0_task(void *arg)
{
    ESP_LOGI(TAG, "Core0 task started (system/network)");
    (void)arg;

    for (;;) {
        /* Later: Time Engine, Network Service, OTA, storage background work */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void core1_task(void *arg)
{
    ESP_LOGI(TAG, "Core1 task started (UI/display)");
    (void)arg;

    for (;;) {
        /* Later: Display compositor, UI Manager, Menu Controller, Notification Manager */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void core_os_start(void)
{
    ESP_LOGI(TAG, "Core OS start");

    if (g_boot_flags & BOOT_FLAG_SAFE_MODE) {
        ESP_LOGW(TAG, "Safe mode: skipping normal service tasks (placeholder)");
        return;
    }

    BaseType_t res0 = xTaskCreatePinnedToCore(core0_task,
                                              "core0_task",
                                              4096,
                                              NULL,
                                              5,
                                              NULL,
                                              0);

    BaseType_t res1 = xTaskCreatePinnedToCore(core1_task,
                                              "core1_task",
                                              4096,
                                              NULL,
                                              5,
                                              NULL,
                                              1);

    if (res0 != pdPASS || res1 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create core tasks: res0=%ld res1=%ld",
                 (long)res0, (long)res1);
        return;
    }

    ESP_LOGI(TAG, "Core OS start complete, tasks running");
}
