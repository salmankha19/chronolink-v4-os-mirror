/*
 * hal_display_max7219.c
 *
 * ChronoLink V4 OS -- MAX7219 LED Matrix Backend
 *
 * STATUS: STUB. Every function returns HAL_ERR_DEV. No rendering,
 * no SPI traffic, no register writes. This file exists only so the
 * router's ops-table contract can be satisfied and the backend can
 * be selected at compile time if a developer is actively working on
 * the driver.
 *
 * Constitution section 12 (honest-stub rule): a function that is not
 * implemented returns an error. Returning HAL_OK from a stub is
 * forbidden. An earlier version of this file returned HAL_OK from
 * every entry point while drawing nothing -- the router would treat
 * the backend as live and callers would get silent no-ops. Now the
 * failures are visible.
 *
 * Kconfig gate: CONFIG_CHRONOLINK_HAL_ALLOW_STUB_BACKENDS. Default
 * off. This backend is not offered in the display-backend choice
 * unless that option is enabled.
 */

#include "hal_display.h"
#include "hal_display_max7219.h"
#include "esp_log.h"

static const char *TAG = "HAL_MAX7219";

/* --------------------------------------------------------------------------
 * Capability reporting
 *
 * Every capability is unsupported: no drawing function works, so nothing
 * can be honestly advertised. HAL_CAP_COUNT is not a valid capability.
 * -------------------------------------------------------------------------- */
hal_status_t HAL_Display_MAX7219_HasCapability(hal_display_cap_t cap)
{
    if (cap >= HAL_CAP_COUNT) {
        ESP_LOGW(TAG, "HasCapability called with invalid cap %d", (int)cap);
        return HAL_ERR_DEV;
    }
    return HAL_ERR_DEV;
}

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */
hal_status_t HAL_Display_MAX7219_Init(void)
{
    ESP_LOGW(TAG, "MAX7219 backend is a stub -- init refusing");
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_MAX7219_Deinit(void)
{
    /* Deinit of a never-inited backend: nothing to release, but the
       backend never worked, so returning OK would imply "we tore down
       something real". Return HAL_ERR_DEV for honesty. */
    return HAL_ERR_DEV;
}

/* --------------------------------------------------------------------------
 * Rendering
 * -------------------------------------------------------------------------- */
hal_status_t HAL_Display_MAX7219_WriteText(const char *text)
{
    (void)text;
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_MAX7219_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    (void)x;
    (void)y;
    (void)color;
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_MAX7219_Clear(void)
{
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_MAX7219_Fill(uint32_t color)
{
    (void)color;
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_MAX7219_Show(void)
{
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_MAX7219_FillRect(uint16_t x, uint16_t y,
                                          uint16_t w, uint16_t h,
                                          uint32_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_MAX7219_BlitRow(uint16_t x, uint16_t y,
                                         const uint32_t *pixels24,
                                         uint16_t len)
{
    (void)x; (void)y; (void)pixels24; (void)len;
    return HAL_ERR_DEV;
}

/* --------------------------------------------------------------------------
 * Runtime panel dimensions
 *
 * Returns 0 (unknown) rather than 8. A "usable" size for a stub would
 * mislead the GFX layer into computing buffer sizes and clip rects for
 * a display that never draws anything.
 * -------------------------------------------------------------------------- */
int HAL_Display_MAX7219_GetWidth(void)
{
    return 0;
}

int HAL_Display_MAX7219_GetHeight(void)
{
    return 0;
}

/* --------------------------------------------------------------------------
 * Ops table exported to the router
 *
 * set_madctl / set_scroll_area / set_scroll_start are NULL -- MAX7219
 * does not support them. The router returns HAL_ERR_DEV if called.
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