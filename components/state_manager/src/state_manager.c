#include "state_manager.h"
#include "ui_state.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "state_mgr";
static QueueHandle_t s_state_queue = NULL;
static UIState s_ui_state = {
    .unix_ts = 0,
    .time_str = "00:00",
    .brightness = 0,
};

void state_manager_init(QueueHandle_t queue)
{
    s_state_queue = queue;
    ESP_LOGI(TAG, "State manager initialized");
}

void state_manager_dispatch(const os_state_msg_t *msg)
{
    if (!msg) return;
    ESP_LOGD(TAG, "Dispatch id=%u value=%lu", (unsigned)msg->id, (unsigned long)msg->value);
}

const UIState *state_get_snapshot(void)
{
    return &s_ui_state;
}

void state_publish_update(const UIState *s)
{
    if (s == NULL) {
        return;
    }

    s_ui_state.unix_ts = s->unix_ts;
    s_ui_state.brightness = s->brightness;
    strncpy(s_ui_state.time_str, s->time_str, sizeof(s_ui_state.time_str) - 1);
    s_ui_state.time_str[sizeof(s_ui_state.time_str) - 1] = '\0';

    if (s_state_queue != NULL) {
        os_state_msg_t msg = { .id = 0, .value = 0 };
        (void)xQueueSend(s_state_queue, &msg, 0);
    }
}
