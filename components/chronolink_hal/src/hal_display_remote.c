#include "hal_display.h"
#include "esp_log.h"

static const char *TAG = "HAL_REMOTE_DISP";

hal_status_t HAL_Display_Remote_Init(void)
{
    ESP_LOGI(TAG, "Remote display backend initialized");
    // TODO: Initialize remote transport/session for display commands.
    return HAL_OK;
}

hal_status_t HAL_Display_Remote_WriteText(const char *text)
{
    (void)text;
    // TODO: Serialize text payload and forward to remote renderer.
    return HAL_OK;
}

hal_status_t HAL_Display_Remote_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    (void)x;
    (void)y;
    (void)color;
    // TODO: Send remote single-pixel draw command.
    return HAL_OK;
}

hal_status_t HAL_Display_Remote_Clear(void)
{
    // TODO: Send remote clear-frame command.
    return HAL_OK;
}

hal_status_t HAL_Display_Remote_Fill(uint32_t color)
{
    (void)color;
    // TODO: Send remote fill-frame command.
    return HAL_OK;
}

hal_status_t HAL_Display_Remote_Show(void)
{
    // TODO: Trigger remote present/flush.
    return HAL_OK;
}