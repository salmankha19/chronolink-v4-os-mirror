#include "state_manager.h"
#include "ui_state.h"           /* canonical declarations for state_get_snapshot and state_publish_update */
#include "esp_log.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "state_manager";

/* Internal UI state (kept private) */
static UIState current = {0, "00:00", 50};

/* Synchronization primitives */
static SemaphoreHandle_t state_lock = NULL;

/* Optional queue handle provided by core_os during initialization */
static QueueHandle_t g_state_queue = NULL;

/* Ensure the mutex exists; idempotent */
static void state_manager_init_once(void)
{
    if (state_lock == NULL) {
        state_lock = xSemaphoreCreateMutex();
        if (state_lock == NULL) {
            ESP_LOGE(TAG, "Failed to create state mutex");
        } else {
            ESP_LOGI(TAG, "State Manager mutex created");
        }
    }
}

/* Public: initialize the state manager with an existing queue handle.
   Records the queue handle and ensures the mutex exists. */
void state_manager_init(QueueHandle_t queue)
{
    if (g_state_queue != NULL) {
        ESP_LOGI(TAG, "state_manager_init: already initialized (queue=%p)", (void*)g_state_queue);
        return;
    }

    if (queue == NULL) {
        ESP_LOGE(TAG, "state_manager_init: provided queue is NULL");
        /* still ensure mutex exists so other APIs remain usable */
        state_manager_init_once();
        return;
    }

    g_state_queue = queue;
    state_manager_init_once();

    ESP_LOGI(TAG, "state_manager_init: initialized with queue=%p", (void*)g_state_queue);
}

/* Public: dispatch one message from the OS state queue into state updates. */
void state_manager_dispatch(const os_state_msg_t *msg)
{
    if (msg == NULL) {
        return;
    }

    state_manager_init_once();

    if (state_lock && xSemaphoreTake(state_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
        switch (msg->id) {
            case 1:
                /* id=1: brightness update */
                current.brightness = (uint8_t)(msg->value & 0xFFu);
                break;
            case 2:
                /* id=2: unix timestamp update */
                current.unix_ts = msg->value;
                break;
            default:
                /* Unknown message id: no state mutation yet. */
                break;
        }
        xSemaphoreGive(state_lock);
    }
}

/* Return a pointer to the current UIState snapshot (read-only).
   Signature must match the declaration in ui_state.h. */
const UIState *state_get_snapshot(void)
{
    state_manager_init_once();

    if (state_lock && xSemaphoreTake(state_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
        /* We intentionally return pointer to internal state; caller must treat as read-only. */
        xSemaphoreGive(state_lock);
    }

    return &current;
}

/* Update the internal UI state in a thread-safe manner and optionally post to queue.
   Signature must match the declaration in ui_state.h. */
void state_publish_update(const UIState *s)
{
    if (!s) {
        return;
    }

    state_manager_init_once();

    if (state_lock && xSemaphoreTake(state_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
        current.unix_ts    = s->unix_ts;
        current.brightness = s->brightness;
        strncpy(current.time_str, s->time_str, sizeof(current.time_str) - 1);
        current.time_str[sizeof(current.time_str) - 1] = '\0';
        xSemaphoreGive(state_lock);
    }

    ESP_LOGI(TAG, "UI state updated: ts=%lu time=%s brightness=%u",
             (unsigned long)current.unix_ts,
             current.time_str,
             current.brightness);

    /* If a state queue was provided at init, optionally post a lightweight notification.
       This is non-blocking and will not fail the update if the queue is full. */
    if (g_state_queue) {
        os_state_msg_t msg = { .id = 0, .value = 0 }; /* populate as needed */
        (void)xQueueSend(g_state_queue, &msg, 0);
    }
}
