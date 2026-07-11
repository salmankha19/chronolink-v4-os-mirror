#include "event_bus.h"
#include "esp_log.h"
#include <string.h>
#include "boot_events.h"

static const char *TAG = "event_bus";
static QueueHandle_t g_event_queue = NULL;

void event_bus_init(QueueHandle_t queue)
{
    g_event_queue = queue;
    if (!g_event_queue) {
        ESP_LOGE(TAG, "event_bus_init: queue is NULL");
    } else {
        ESP_LOGI(TAG, "Event Bus initialized (queue=%p)", (void*)g_event_queue);
    }
}

void event_bus_publish(const event_t *evt)
{
    if (!evt || !g_event_queue) return;
    if (xQueueSend(g_event_queue, evt, 0) != pdPASS) {
        ESP_LOGW(TAG, "event_bus_publish: queue full, event dropped");
    }
}

bool event_bus_receive(event_t *evt, TickType_t timeout)
{
    if (!evt || !g_event_queue) 
        return false;
    return xQueueReceive(g_event_queue, evt, timeout) == pdPASS;
}
