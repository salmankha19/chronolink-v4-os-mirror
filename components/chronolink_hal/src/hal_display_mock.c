#include "esp_log.h"
#include "hal_display.h"

static const char *TAG = "DISPLAY_CAPS";
__attribute__((used, section(".flash.rodata"))) const char keep_display_caps[] = "DISPLAY_CAPS";
__attribute__((used)) static const char *keep_display_caps_ref = keep_display_caps;

hal_status_t HAL_Display_Mock_Init(void)
{
    ESP_LOGI(TAG, "Mock display init");
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_Deinit(void)
{
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    (void)x;
    (void)y;
    (void)color;
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_Fill(uint32_t color)
{
    (void)color;
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_BlitRow(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len)
{
    (void)x;
    (void)y;
    (void)pixels24;
    (void)len;
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_Clear(void)
{
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_Show(void)
{
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_WriteText(const char *text)
{
    (void)text;
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_HasCapability(hal_display_cap_t cap)
{
    (void)cap;
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_SetMadctl(uint8_t madctl)
{
    (void)madctl;
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_SetScrollArea(uint16_t tfa, uint16_t vsa, uint16_t bfa)
{
    (void)tfa;
    (void)vsa;
    (void)bfa;
    return HAL_OK;
}

hal_status_t HAL_Display_Mock_SetScrollStart(uint16_t vss)
{
    (void)vss;
    return HAL_OK;
}

/* --------------------------------------------------------------------------
 * Runtime panel dimensions (mock: report 0 — headless)
 * -------------------------------------------------------------------------- */
int HAL_Display_Mock_GetWidth(void)
{
    return 0;
}

int HAL_Display_Mock_GetHeight(void)
{
    return 0;
}

/* --------------------------------------------------------------------------
 * Ops table exported to the router
 * -------------------------------------------------------------------------- */
const display_backend_ops_t HAL_DISPLAY_MOCK_OPS = {
    .name              = "mock",
    .init              = HAL_Display_Mock_Init,
    .deinit            = HAL_Display_Mock_Deinit,
    .draw_pixel        = HAL_Display_Mock_DrawPixel,
    .fill              = HAL_Display_Mock_Fill,
    .fill_rect         = HAL_Display_Mock_FillRect,
    .blit_row          = HAL_Display_Mock_BlitRow,
    .clear             = HAL_Display_Mock_Clear,
    .show              = HAL_Display_Mock_Show,
    .write_text        = HAL_Display_Mock_WriteText,
    .has_capability    = HAL_Display_Mock_HasCapability,
    .set_madctl        = HAL_Display_Mock_SetMadctl,
    .set_scroll_area   = HAL_Display_Mock_SetScrollArea,
    .set_scroll_start  = HAL_Display_Mock_SetScrollStart,
    .get_width         = HAL_Display_Mock_GetWidth,
    .get_height        = HAL_Display_Mock_GetHeight,
};