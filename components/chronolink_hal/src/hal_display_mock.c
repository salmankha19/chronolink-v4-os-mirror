#include "esp_log.h"
#include "hal_display.h"

static const char *TAG = "DISPLAY_CAPS";

/* Force the literal into the final binary even if the linker would discard it */
__attribute__((used)) static const char keep_display_caps[] = "DISPLAY_CAPS";

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
