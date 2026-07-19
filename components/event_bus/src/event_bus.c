#include "event_bus.h"
#include "esp_log.h"
#include <string.h>
#include "boot_events.h"

static const char *TAG = "event_bus";
static QueueHandle_t g_event_queue = NULL;

/* If the caller doesn't provide a queue, create an internal one with this size.
   Tune as needed for your workload. */
#define DEFAULT_EVENT_QUEUE_LEN 64
#define ENQUEUE_TIMEOUT_MS 10

void event_bus_init(QueueHandle_t queue)
{
    if (queue) {
        g_event_queue = queue;
        ESP_LOGI(TAG, "Event Bus initialized (external queue=%p)", (void*)g_event_queue);
        return;
    }

    /* Create an internal queue if none provided */
    g_event_queue = xQueueCreate(DEFAULT_EVENT_QUEUE_LEN, sizeof(event_t));
    if (!g_event_queue) {
        ESP_LOGE(TAG, "event_bus_init: failed to create internal queue");
    } else {
        ESP_LOGI(TAG, "Event Bus initialized (internal queue=%p, len=%d)", (void*)g_event_queue, DEFAULT_EVENT_QUEUE_LEN);
    }
}

void event_bus_publish(const event_t *evt)
{
    if (!evt) return;

    if (!g_event_queue) {
        ESP_LOGW(TAG, "event_bus_publish: no queue available, event dropped");
        return;
    }

    /* Try to enqueue with a short timeout to provide brief backpressure.
       This avoids immediate drops during short bursts; increase timeout
       or queue length if needed. Consider coalescing frequent events. */
    if (xQueueSend(g_event_queue, evt, pdMS_TO_TICKS(ENQUEUE_TIMEOUT_MS)) != pdPASS) {
        ESP_LOGW(TAG, "event_bus_publish: queue full after %dms, event dropped", ENQUEUE_TIMEOUT_MS);
    }
}

bool event_bus_receive(event_t *evt, TickType_t timeout)
{
    if (!evt || !g_event_queue)
        return false;
    return xQueueReceive(g_event_queue, evt, timeout) == pdPASS;
}
