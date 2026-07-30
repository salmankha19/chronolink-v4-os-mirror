/* template_custom_display.c
 *
 * Minimal custom display driver template for ChronoLink V4 OS.
 * Copy this file, rename, implement the functions, and call
 * chronolink_driver_register() before HAL_Display_Init().
 */

#include "chronolink_driver_sdk.h"
#include "esp_log.h"

static const char *TAG = "custom_display";

static hal_status_t my_init(void)
{
    ESP_LOGI(TAG, "Custom display init");
    return HAL_OK;
}

static hal_status_t my_deinit(void)
{
    return HAL_OK;
}

static hal_status_t my_draw_pixel(uint16_t x, uint16_t y, uint32_t color)
{
    (void)x; (void)y; (void)color;
    return HAL_OK;
}

static hal_status_t my_fill(uint32_t color)
{
    (void)color;
    return HAL_OK;
}

static hal_status_t my_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
    return HAL_OK;
}

static hal_status_t my_blit_row(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len)
{
    (void)x; (void)y; (void)pixels24; (void)len;
    return HAL_OK;
}

static hal_status_t my_clear(void)
{
    return HAL_OK;
}

static hal_status_t my_show(void)
{
    return HAL_OK;
}

static hal_status_t my_write_text(const char *text)
{
    (void)text;
    return HAL_ERR_DEV; /* not supported */
}

static hal_status_t my_has_capability(hal_display_cap_t cap)
{
    switch (cap) {
    case HAL_CAP_DRAW_PIXEL:
    case HAL_CAP_FILL:
    case HAL_CAP_CLEAR:
    case HAL_CAP_SHOW:
        return HAL_OK;
    default:
        return HAL_ERR_DEV;
    }
}

static hal_status_t my_set_madctl(uint8_t madctl)
{
    (void)madctl;
    return HAL_ERR_DEV;
}

static hal_status_t my_set_scroll_area(uint16_t tfa, uint16_t vsa, uint16_t bfa)
{
    (void)tfa; (void)vsa; (void)bfa;
    return HAL_ERR_DEV;
}

static hal_status_t my_set_scroll_start(uint16_t vss)
{
    (void)vss;
    return HAL_ERR_DEV;
}

static int my_get_width(void)  { return 320; }
static int my_get_height(void) { return 240; }

/* --------------------------------------------------------------------------
 * Public registration helper
 * -------------------------------------------------------------------------- */
static const chronolink_display_driver_t s_my_driver = {
    .name            = "template_custom",
    .init            = my_init,
    .deinit          = my_deinit,
    .draw_pixel      = my_draw_pixel,
    .fill            = my_fill,
    .fill_rect       = my_fill_rect,
    .blit_row        = my_blit_row,
    .clear           = my_clear,
    .show            = my_show,
    .write_text      = my_write_text,
    .has_capability  = my_has_capability,
    .set_madctl      = my_set_madctl,
    .set_scroll_area = my_set_scroll_area,
    .set_scroll_start = my_set_scroll_start,
    .get_width       = my_get_width,
    .get_height      = my_get_height,
};

void template_custom_display_register(void)
{
    hal_status_t s = chronolink_driver_register(&s_my_driver);
    if (s != HAL_OK) {
        ESP_LOGE(TAG, "Failed to register custom driver");
    } else {
        ESP_LOGI(TAG, "Custom driver registered");
    }
}
