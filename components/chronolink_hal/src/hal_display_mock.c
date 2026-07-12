#include "esp_log.h"
#include "hal_display.h"

static const char *TAG = "DISPLAY_CAPS";
__attribute__((used, section(".flash.rodata"))) static const char keep_display_caps[] = "DISPLAY_CAPS";
__attribute__((used)) static const char *keep_display_caps_ref = keep_display_caps;

hal_status_t HAL_Display_Init(void)
{
    ESP_LOGI(TAG, "Mock display init");
    return HAL_OK;
}

hal_status_t HAL_Display_HasCapability(hal_display_cap_t cap)
{
    (void)cap;
    return HAL_OK;
}
