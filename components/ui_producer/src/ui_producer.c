#include "ui_producer.h"
#include "display_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui_producer";

void ui_producer_start(void)
{
    ESP_LOGI(TAG, "UI producer started");
    display_manager_init();
}
