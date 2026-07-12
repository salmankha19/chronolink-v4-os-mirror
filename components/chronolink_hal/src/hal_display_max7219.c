#include "hal_display.h"
#include "esp_log.h"

static const char *TAG = "HAL_MAX7219";

hal_status_t HAL_Display_MAX7219_Init(void)
{
    ESP_LOGI(TAG, "MAX7219 backend initialized");
    // TODO: Configure MAX7219 registers and brightness.
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_WriteText(const char *text)
{
    (void)text;
    // TODO: Implement text scroll/render for MAX7219 matrix.
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    (void)x;
    (void)y;
    (void)color;
    // TODO: Map pixel coordinates to MAX7219 row/column buffers.
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_Clear(void)
{
    return HAL_Display_MAX7219_Fill(0U);
}

hal_status_t HAL_Display_MAX7219_Fill(uint32_t color)
{
    (void)color;
    // TODO: Implement full display buffer fill for MAX7219.
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_Show(void)
{
    // TODO: Flush shadow buffer to MAX7219 over SPI.
    return HAL_OK;
}
