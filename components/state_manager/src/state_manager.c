#include "state_manager.h"
#include "esp_log.h"

static const char *TAG = "state_mgr";

void state_manager_init(QueueHandle_t queue)
{
    (void)queue;
    ESP_LOGI(TAG, "State manager initialized");
}

void state_manager_dispatch(const os_state_msg_t *msg)
{
    if (!msg) return;
    ESP_LOGD(TAG, "Dispatch id=%u value=%lu", msg->id, (unsigned long)msg->value);
}
