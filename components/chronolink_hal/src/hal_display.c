#include "sdkconfig.h"
#include "hal_display.h"
#include "esp_log.h"

static const char *TAG = "HAL_DISPLAY";

hal_status_t HAL_Display_Init(void)
{
#if CONFIG_BOARD_DISPLAY_ST7796S
    ESP_LOGI(TAG, "Initializing ST7796S display");
    return HAL_Display_ST7796S_Init();

#elif CONFIG_BOARD_DISPLAY_MAX7219
    ESP_LOGI(TAG, "Initializing MAX7219 display");
    return HAL_Display_MAX7219_Init();

#elif CONFIG_BOARD_DISPLAY_REMOTE
    ESP_LOGI(TAG, "Initializing Remote Display backend");
    return HAL_Display_Remote_Init();

#else
    ESP_LOGI(TAG, "No display selected");
    return HAL_OK;
#endif
}
