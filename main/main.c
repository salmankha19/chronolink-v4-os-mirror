#include "project_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "core_os.h"
#include "display_api.h"
#include "boot_manager.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "ChronoLink v4 OS - ESP32-S3 Starting");

    // Boot Manager handles:
    // - NVS mount
    // - FATFS mount
    // - OTA partition detection
    // - Core OS init + start
    if (!boot_manager_init()) {
        ESP_LOGE(TAG, "Boot manager failed, entering safe loop");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "ChronoLink initialized successfully");

    // Main loop (optional)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
