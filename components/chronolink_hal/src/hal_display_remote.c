/*
 * hal_display_remote.c
 *
 * ChronoLink V4 OS -- Remote Display Backend
 *
 * STATUS: STUB. Every function returns HAL_ERR_DEV. No network
 * transport, no protocol, no serialisation. This file exists only
 * so the router's ops-table contract can be satisfied and the
 * backend can be selected at compile time if a developer is
 * actively working on the protocol.
 *
 * Constitution section 12 (honest-stub rule): a function that is not
 * implemented returns an error. Returning HAL_OK from a stub is
 * forbidden. An earlier version of this file returned HAL_OK from
 * every entry point while sending nothing -- the router would treat
 * the backend as live and callers would get silent no-ops. Now the
 * failures are visible.
 *
 * Kconfig gate: CONFIG_CHRONOLINK_HAL_ALLOW_STUB_BACKENDS. Default
 * off. This backend is not offered in the display-backend choice
 * unless that option is enabled.
 */

#include "hal_display.h"
#include "hal_display_remote.h"

#include "esp_log.h"

static const char *TAG = "HAL_REMOTE_DISP";

hal_status_t HAL_Display_Remote_HasCapability(hal_display_cap_t cap)
{
    if (cap >= HAL_CAP_COUNT) {
        ESP_LOGW(TAG, "HasCapability called with invalid cap %d", (int)cap);
        return HAL_ERR_DEV;
    }
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_Remote_Init(void)
{
    ESP_LOGW(TAG, "Remote display backend is a stub -- init refusing");
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_Remote_WriteText(const char *text)
{
    (void)text;
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_Remote_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    (void)x;
    (void)y;
    (void)color;
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_Remote_Clear(void)
{
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_Remote_Fill(uint32_t color)
{
    (void)color;
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_Remote_Show(void)
{
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_Remote_Deinit(void)
{
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_Remote_FillRect(uint16_t x, uint16_t y,
                                         uint16_t w, uint16_t h,
                                         uint32_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
    return HAL_ERR_DEV;
}

hal_status_t HAL_Display_Remote_BlitRow(uint16_t x, uint16_t y,
                                        const uint32_t *pixels24,
                                        uint16_t len)
{
    (void)x; (void)y; (void)pixels24; (void)len;
    return HAL_ERR_DEV;
}

/* --------------------------------------------------------------------------
 * Runtime panel dimensions
 *
 * Returns 0 (unknown). A remote panel's size is only known after a
 * handshake, which this backend does not perform.
 * -------------------------------------------------------------------------- */
int HAL_Display_Remote_GetWidth(void)
{
    return 0;
}

int HAL_Display_Remote_GetHeight(void)
{
    return 0;
}

const display_backend_ops_t HAL_DISPLAY_REMOTE_OPS = {
    .name              = "remote",
    .init              = HAL_Display_Remote_Init,
    .deinit            = HAL_Display_Remote_Deinit,
    .draw_pixel        = HAL_Display_Remote_DrawPixel,
    .fill              = HAL_Display_Remote_Fill,
    .fill_rect         = HAL_Display_Remote_FillRect,
    .blit_row          = HAL_Display_Remote_BlitRow,
    .clear             = HAL_Display_Remote_Clear,
    .show              = HAL_Display_Remote_Show,
    .write_text        = HAL_Display_Remote_WriteText,
    .has_capability    = HAL_Display_Remote_HasCapability,
    .set_madctl        = NULL,
    .set_scroll_area   = NULL,
    .set_scroll_start  = NULL,
    .get_width         = HAL_Display_Remote_GetWidth,
    .get_height        = HAL_Display_Remote_GetHeight,
};