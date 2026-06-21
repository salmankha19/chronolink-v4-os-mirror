#include "project_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "core_os.h"
#include "display_api.h"

static const char *TAG = "main";

void app_main(void) {
  ESP_LOGI(TAG, "ChronoLink v4 OS - ESP32-S3 Starting");
  
  // Initialize core OS subsystems
  core_os_init();
  core_os_start();
  
  ESP_LOGI(TAG, "ChronoLink initialized successfully");
  
  // Main event loop
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
