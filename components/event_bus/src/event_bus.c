#include "event_bus.h"
#include "esp_log.h"
#include <string.h>
#include "boot_events.h"

static const char *TAG = "event_bus";
static QueueHandle_t g_event_queue = NULL;

#define DEFAULT_EVENT_QUEUE_LEN 64

/* Subscriber fan-out (PR 5b-1).
 *
 * Each entry is one (queue, type) rule, added by event_bus_subscribe() and
 * served by event_bus_dispatch(). Fixed size, no allocation. Registration is
 * expected during init only: it is not synchronised against dispatch. */
#define EVENT_BUS_MAX_SUBSCRIBERS 8

static struct {
    QueueHandle_t q;
    event_type_t  type;
    bool          used;
    uint32_t      drop_count;
} s_subs[EVENT_BUS_MAX_SUBSCRIBERS];

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

/* Register one (queue, type) rule. Idempotent: registering the same pair
 * twice succeeds and changes nothing. See event_bus.h for the threading
 * contract (call during init; not safe against event_bus_dispatch()). */
bool event_bus_subscribe(QueueHandle_t q, event_type_t type)
{
    if (!q) {
        ESP_LOGW(TAG, "event_bus_subscribe: NULL queue, type=%d rejected", (int)type);
        return false;
    }

    for (size_t i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (s_subs[i].used && s_subs[i].q == q && s_subs[i].type == type) {
            return true; /* already registered */
        }
    }

    for (size_t i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (!s_subs[i].used) {
            s_subs[i].q = q;
            s_subs[i].type = type;
            s_subs[i].drop_count = 0;
            s_subs[i].used = true;
            ESP_LOGI(TAG, "subscriber registered (queue=%p, type=%d, slot=%u)",
                     (void*)q, (int)type, (unsigned)i);
            return true;
        }
    }

    ESP_LOGE(TAG, "event_bus_subscribe: table full (%d), queue=%p type=%d rejected",
             EVENT_BUS_MAX_SUBSCRIBERS, (void*)q, (int)type);
    return false;
}

/* Forward a copy of evt to every subscriber queue registered for its type.
 * Never blocks: a full subscriber queue drops that copy and counts it. */
void event_bus_dispatch(const event_t *evt)
{
    if (!evt) return;

    for (size_t i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (!s_subs[i].used || s_subs[i].type != evt->type) {
            continue;
        }

        if (xQueueSend(s_subs[i].q, evt, 0) == pdPASS) {
            continue;
        }

        s_subs[i].drop_count++;
        if ((s_subs[i].drop_count % 100U) == 1U) {
            ESP_LOGW(TAG, "event_bus_dispatch: subscriber queue full "
                          "(queue=%p, type=%d, total dropped=%lu)",
                     (void*)s_subs[i].q, (int)evt->type,
                     (unsigned long)s_subs[i].drop_count);
        }
    }
}