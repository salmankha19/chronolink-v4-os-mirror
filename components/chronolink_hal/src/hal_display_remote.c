#include "hal_display.h"
#include "hal_display_remote.h"

#include "esp_log.h"

static const char *TAG = "HAL_REMOTE_DISP";

/* Add this near the top, after includes */
hal_status_t HAL_Display_Remote_HasCapability(hal_display_cap_t cap)
{
    switch (cap) {
    case HAL_CAP_TEXT:
    case HAL_CAP_BITMAP:
    case HAL_CAP_CLEAR:
    case HAL_CAP_SHOW:
        return HAL_OK;

    case HAL_CAP_DRAW_PIXEL:
    case HAL_CAP_FILL:
    case HAL_CAP_ROTATION:
    case HAL_CAP_BRIGHTNESS:
    default:
        return HAL_ERR_DEV;
    }
}

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