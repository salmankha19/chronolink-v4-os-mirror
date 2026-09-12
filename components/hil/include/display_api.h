#ifndef HIL_DISPLAY_API_H
#define HIL_DISPLAY_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool display_init(void);
bool display_deinit(void);
bool display_clear(void);

/* 24-bit RGB (0xRRGGBB) -- matches chronolink_hal's HAL_Display_*
   contract exactly. NOT uint16_t/RGB565 -- that's hal_gfx's internal
   format one level below this, converted for you. */
bool display_draw_pixel(int x, int y, uint32_t color);
bool display_fill(uint32_t color);

/* Renders via font_engine using the built-in 8x8 font (id 0), which
   this function registers on first use if not already registered.
   Text color is fixed white (0xFFFF RGB565) -- if you need other
   colors, call font_engine_draw_text() directly with a chosen font_id
   and hal_gfx_color_t. */
bool display_draw_text(int x, int y, const char *text);

/* NOT IMPLEMENTED: chronolink_hal's hal_display.h has no
   HAL_Display_SetBrightness (or equivalent PWM-on-backlight-pin)
   function today. Returns false always. Wiring this up would mean
   adding real backlight PWM control to chronolink_hal first --
   that's HDL work, not something this adapter can fake. */
bool display_set_brightness(uint8_t level);

uint16_t display_get_width(void);
uint16_t display_get_height(void);

#ifdef __cplusplus
}
#endif

#endif /* HIL_DISPLAY_API_H */
