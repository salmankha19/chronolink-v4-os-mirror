/*
 * hal_display_max7219.c
 *
 * ChronoLink V4 OS — MAX7219 LED Matrix Backend
 *
 * NOTE: The rendering functions below are stubs. They return HAL_OK but
 * do nothing. A real MAX7219 driver (register init, pixel mapping, text
 * scrolling, SPI flush) is a separate future task. This file's current
 * purpose is to satisfy the router's ops-table contract so the backend
 * can be selected via Kconfig without link errors.
 */

#include "hal_display.h"
#include "hal_display_max7219.h"
#include "esp_log.h"

static const char *TAG = "HAL_MAX7219";

/* --------------------------------------------------------------------------
 * Capability reporting
 * -------------------------------------------------------------------------- */
hal_status_t HAL_Display_MAX7219_HasCapability(hal_display_cap_t cap)
{
    switch (cap) {
    case HAL_CAP_TEXT:
    case HAL_CAP_BITMAP:
    case HAL_CAP_BRIGHTNESS:
    case HAL_CAP_CLEAR:
    case HAL_CAP_SHOW:
        return HAL_OK;

    case HAL_CAP_DRAW_PIXEL:
    case HAL_CAP_FILL:
    case HAL_CAP_ORIENTATION:
    case HAL_CAP_SCROLL:
    default:
        return HAL_ERR_DEV;
    }
}

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */
hal_status_t HAL_Display_MAX7219_Init(void)
{
    ESP_LOGI(TAG, "MAX7219 backend initialized");
    /* TODO: Configure MAX7219 registers and brightness. */
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_Deinit(void)
{
    /* TODO: Send shutdown command to MAX7219 chain. */
    return HAL_OK;
}

/* --------------------------------------------------------------------------
 * Rendering (stubs — see file header)
 * -------------------------------------------------------------------------- */
hal_status_t HAL_Display_MAX7219_WriteText(const char *text)
{
    (void)text;
    /* TODO: Implement text scroll/render for MAX7219 matrix. */
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    (void)x;
    (void)y;
    (void)color;
    /* TODO: Map pixel coordinates to MAX7219 row/column buffers. */
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_Clear(void)
{
    return HAL_Display_MAX7219_Fill(0U);
}

hal_status_t HAL_Display_MAX7219_Fill(uint32_t color)
{
    (void)color;
    /* TODO: Implement full display buffer fill for MAX7219. */
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_Show(void)
{
    /* TODO: Flush shadow buffer to MAX7219 over SPI. */
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_FillRect(uint16_t x, uint16_t y,
                                          uint16_t w, uint16_t h,
                                          uint32_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
    /* TODO: Implement rectangle fill for MAX7219. */
    return HAL_OK;
}

hal_status_t HAL_Display_MAX7219_BlitRow(uint16_t x, uint16_t y,
                                         const uint32_t *pixels24,
                                         uint16_t len)
{
    (void)x; (void)y; (void)pixels24; (void)len;
    /* TODO: Implement row blit for MAX7219. */
    return HAL_OK;
}

/* --------------------------------------------------------------------------
 * Runtime panel dimensions
 *
 * MAX7219 is chain-dependent; return 8 as a single-module default.
 * Override at the app layer if you know the chain length.
 * -------------------------------------------------------------------------- */
int HAL_Display_MAX7219_GetWidth(void)
{
    return 8;
}

int HAL_Display_MAX7219_GetHeight(void)
{
    return 8;
}

/* --------------------------------------------------------------------------
 * Ops table exported to the router
 *
 * set_madctl / set_scroll_area / set_scroll_start are NULL — MAX7219
 * doesn't support them. The router returns HAL_ERR_DEV if called.
 * -------------------------------------------------------------------------- */
const display_backend_ops_t HAL_DISPLAY_MAX7219_OPS = {
    .name              = "max7219",
    .init              = HAL_Display_MAX7219_Init,
    .deinit            = HAL_Display_MAX7219_Deinit,
    .draw_pixel        = HAL_Display_MAX7219_DrawPixel,
    .fill              = HAL_Display_MAX7219_Fill,
    .fill_rect         = HAL_Display_MAX7219_FillRect,
    .blit_row          = HAL_Display_MAX7219_BlitRow,
    .clear             = HAL_Display_MAX7219_Clear,
    .show              = HAL_Display_MAX7219_Show,
    .write_text        = HAL_Display_MAX7219_WriteText,
    .has_capability    = HAL_Display_MAX7219_HasCapability,
    .set_madctl        = NULL,
    .set_scroll_area   = NULL,
    .set_scroll_start  = NULL,
    .get_width         = HAL_Display_MAX7219_GetWidth,
    .get_height        = HAL_Display_MAX7219_GetHeight,
};