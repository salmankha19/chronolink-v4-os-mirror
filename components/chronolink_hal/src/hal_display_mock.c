#include "esp_log.h"
#include "hal_display.h"

static const char *TAG = "DISPLAY_CAPS";
__attribute__((used, section(".flash.rodata"))) static const char keep_display_caps[] = "DISPLAY_CAPS";
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
