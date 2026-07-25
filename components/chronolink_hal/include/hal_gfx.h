/*
 * hal_gfx.h
 *
 * ChronoLink V4 — Graphics primitives layer (Phase 2)
 *
 * hal_gfx provides clipped drawing primitives built on top of the HAL
 * display router.  hal_gfx uses 16-bit RGB565 as its internal pixel
 * format (hal_gfx_color_t). The HAL router/backends are expected to
 * implement FillRect and BlitRow in 24-bit RGB (0xRRGGBB).
 */

#ifndef HAL_GFX_H
#define HAL_GFX_H

#include <stdint.h>
#include <stdbool.h>
#include "hal_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Types
 * -------------------------------------------------------------------------- */

typedef uint16_t hal_gfx_color_t; /* RGB565 */

typedef struct {
    int16_t x;
    int16_t y;
} hal_gfx_point_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w; /* width in pixels (count) */
    int16_t h; /* height in pixels (count) */
} hal_gfx_rect_t;

/* --------------------------------------------------------------------------
 * Configuration helpers (override via build system if needed)
 * -------------------------------------------------------------------------- */

/* Display bounds used for clipping. Prefer to define these from Kconfig. */
#ifndef HAL_GFX_DISPLAY_WIDTH
#define HAL_GFX_DISPLAY_WIDTH  320
#endif

#ifndef HAL_GFX_DISPLAY_HEIGHT
#define HAL_GFX_DISPLAY_HEIGHT 480
#endif

/* Maximum temporary row buffer used by HAL_GFX_Blit when converting rows.
 * If your target has limited stack, reduce this or make it static/heap.
 */
#ifndef HAL_GFX_MAX_ROW_BUF
#define HAL_GFX_MAX_ROW_BUF HAL_GFX_DISPLAY_WIDTH
#endif

/* --------------------------------------------------------------------------
 * Clipping API
 * -------------------------------------------------------------------------- */

void HAL_GFX_SetClip(int16_t x, int16_t y, int16_t w, int16_t h);
void HAL_GFX_ResetClip(void);
bool HAL_GFX_GetClip(hal_gfx_rect_t *clip);

/* --------------------------------------------------------------------------
 * Low-level pixel / fill (clipped)
 * -------------------------------------------------------------------------- */

/* Draw a single pixel (RGB565). Clipped to current clip or screen bounds. */
void HAL_GFX_DrawPixel(int16_t x, int16_t y, hal_gfx_color_t color);

/* Fill a rectangle (w,h are pixel counts). Delegates to HAL_Display_FillRect. */
void HAL_GFX_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, hal_gfx_color_t color);

/* Alias for region fill */
#define HAL_GFX_FillRegion HAL_GFX_FillRect

/* --------------------------------------------------------------------------
 * Lines (Bresenham)
 * -------------------------------------------------------------------------- */

void HAL_GFX_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, hal_gfx_color_t color);
void HAL_GFX_DrawHLine(int16_t x, int16_t y, int16_t w, hal_gfx_color_t color);
void HAL_GFX_DrawVLine(int16_t x, int16_t y, int16_t h, hal_gfx_color_t color);

/* --------------------------------------------------------------------------
 * Rectangles
 * -------------------------------------------------------------------------- */

void HAL_GFX_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, hal_gfx_color_t color);

/* --------------------------------------------------------------------------
 * Circles (midpoint)
 * -------------------------------------------------------------------------- */

void HAL_GFX_DrawCircle(int16_t cx, int16_t cy, int16_t r, hal_gfx_color_t color);
void HAL_GFX_FillCircle(int16_t cx, int16_t cy, int16_t r, hal_gfx_color_t color);

/* --------------------------------------------------------------------------
 * Rounded rectangles
 * -------------------------------------------------------------------------- */

void HAL_GFX_DrawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, hal_gfx_color_t color);
void HAL_GFX_FillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, hal_gfx_color_t color);

/* --------------------------------------------------------------------------
 * Bitmaps / Blitting (clipped)
 *
 * Source buffer is row-major in hal_gfx_color_t (RGB565). The function
 * converts to backend 24-bit RGB and uses HAL_Display_BlitRow when available.
 * -------------------------------------------------------------------------- */

void HAL_GFX_Blit(const hal_gfx_color_t *src,
                  int16_t src_w, int16_t src_h,
                  int16_t dst_x, int16_t dst_y,
                  int16_t src_x, int16_t src_y,
                  int16_t src_w_clip, int16_t src_h_clip);

/* --------------------------------------------------------------------------
 * Color helpers
 * -------------------------------------------------------------------------- */

/* Pack 8-bit RGB into RGB565 */
static inline hal_gfx_color_t HAL_GFX_ColorFromRGB(uint8_t r, uint8_t g, uint8_t b)
{
    return (hal_gfx_color_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Expand RGB565 to 24-bit 0xRRGGBB */
static inline uint32_t HAL_GFX_ColorToBackend24(hal_gfx_color_t c)
{
    uint8_t r5 = (c >> 11) & 0x1F;
    uint8_t g6 = (c >> 5) & 0x3F;
    uint8_t b5 = c & 0x1F;
    uint8_t r8 = (r5 << 3) | (r5 >> 2);
    uint8_t g8 = (g6 << 2) | (g6 >> 4);
    uint8_t b8 = (b5 << 3) | (b5 >> 2);
    return ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8;
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_GFX_H */
