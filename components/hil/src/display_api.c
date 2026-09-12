/*
 * display_api.c
 *
 * Thin adapter over chronolink_hal's HAL_Display_* (hal_display.h) and
 * the font_engine text renderer. Which physical panel driver actually
 * runs is decided entirely by chronolink_hal's own Kconfig choice
 * (ChronoLink Display Configuration -> Select display driver) -- this
 * file never touches SPI/I2C or a specific panel directly, and never
 * duplicates that driver-selection logic.
 */
#include "display_api.h"
#include "hal_display.h"
#include "hal_gfx.h"
#include "font_engine.h"
#include "font_builtin_8x8.h"

static bool s_font_registered = false;

bool display_init(void)
{
    if (HAL_Display_Init() != HAL_OK) {
        return false;
    }
    if (!s_font_registered) {
        /* Idempotent in effect even if called again after a
           deinit/reinit cycle -- font_engine_register() just fails
           harmlessly if id 0 is already taken. */
        s_font_registered = font_engine_register(&FONT_BUILTIN_8X8);
    }
    return true;
}

bool display_deinit(void)
{
    return HAL_Display_Deinit() == HAL_OK;
}

bool display_clear(void)
{
    return HAL_Display_Clear() == HAL_OK;
}

bool display_draw_pixel(int x, int y, uint32_t color)
{
    return HAL_Display_DrawPixel((uint16_t)x, (uint16_t)y, color) == HAL_OK;
}

bool display_fill(uint32_t color)
{
    return HAL_Display_Fill(color) == HAL_OK;
}

bool display_draw_text(int x, int y, const char *text)
{
    if (!text) return false;
    if (!s_font_registered) {
        /* display_init() wasn't called, or font registration failed
           earlier -- try once more rather than silently drawing
           nothing. */
        s_font_registered = font_engine_register(&FONT_BUILTIN_8X8);
        if (!s_font_registered) return false;
    }
    font_engine_draw_text((int16_t)x, (int16_t)y,
                           HAL_GFX_ColorFromRGB(0xFF, 0xFF, 0xFF),
                           /* font_id */ 0, text);
    return true;
}

bool display_set_brightness(uint8_t level)
{
    (void)level;
    /* See display_api.h: chronolink_hal has no brightness/backlight-PWM
       API today. Not faked here. */
    return false;
}

uint16_t display_get_width(void)
{
    return (uint16_t)HAL_Display_GetWidth();
}

uint16_t display_get_height(void)
{
    return (uint16_t)HAL_Display_GetHeight();
}
