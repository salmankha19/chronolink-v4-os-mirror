#include "event_bus.h"
#include "esp_log.h"
#include <string.h>
#include "boot_events.h"

static const char *TAG = "event_bus";
static QueueHandle_t g_event_queue = NULL;

#define DEFAULT_EVENT_QUEUE_LEN 64

void event_bus_init(QueueHandle_t queue)
{
    if (queue) {
        g_event_queue = queue;
        ESP_LOGI(TAG, "Event Bus initialized (external queue=%p)", (void*)g_event_queue);
        return;
    }

    g_event_queue = xQueueCreate(DEFAULT_EVENT_QUEUE_LEN, sizeof(event_t));
    if (!g_event_queue) {
        ESP_LOGE(TAG, "event_bus_init: failed to create internal queue");
    } else {
        ESP_LOGI(TAG, "Event Bus initialized (internal queue=%p, len=%d)",
                 (void*)g_event_queue, DEFAULT_EVENT_QUEUE_LEN);
    }
}

/* Publish policy: never block the caller.
 *
 * Old version waited up to 10 ms for space, which stalled the FreeRTOS
 * timer service task once the 32-slot queue filled (~32 s at 1 Hz).
 * New policy: zero-timeout send; on full, evict oldest and retry once. */
void event_bus_publish(const event_t *evt)
{
    if (!evt) return;

    if (!g_event_queue) {
        ESP_LOGW(TAG, "event_bus_publish: no queue available, event dropped");
        return;
    }

    static uint32_t s_drop_count = 0;

    if (xQueueSend(g_event_queue, evt, 0) == pdPASS) {
        return;
    }

    event_t discarded;
    if (xQueueReceive(g_event_queue, &discarded, 0) == pdPASS) {
        s_drop_count++;
        if ((s_drop_count % 100U) == 1U) {
            ESP_LOGW(TAG, "event_bus_publish: queue full, dropped oldest "
                          "(total dropped=%lu, last dropped type=%d)",
                     (unsigned long)s_drop_count, (int)discarded.type);
        }
        if (xQueueSend(g_event_queue, evt, 0) == pdPASS) {
            return;
        }
    }

    s_drop_count++;
    if ((s_drop_count % 100U) == 1U) {
        ESP_LOGD(TAG, "event_bus_publish: queue full, event dropped "
                      "(total dropped=%lu)", (unsigned long)s_drop_count);
    }
}

bool event_bus_receive(event_t *evt, TickType_t timeout)
{
    if (!evt || !g_event_queue)
        return false;
    return xQueueReceive(g_event_queue, evt, timeout) == pdPASS;
}