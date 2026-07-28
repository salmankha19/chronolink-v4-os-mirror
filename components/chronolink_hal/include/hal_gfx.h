/*
 * hal_gfx.h
 *
 * ChronoLink V4 — Graphics primitives layer (Phase 2)
 *
 * Backend-agnostic, efficient, clipped drawing built on the HAL display router.
 *
 * Design goals:
 *   - Internal pixel format: RGB565 (hal_gfx_color_t).
 *   - Router/backends consume 24-bit RGB packed as 0xRRGGBB in uint32_t.
 *   - Bounded stack, no dynamic allocation, no logging in hot paths.
 *   - Bulk ops routed to backend DMA-friendly APIs (FillRect, BlitRow).
 *
 * Resolution and configuration:
 *   - Default compile-time resolution: 480x320 (override via build system).
 *   - Override by defining HAL_GFX_DISPLAY_WIDTH and HAL_GFX_DISPLAY_HEIGHT
 *     at compile time (e.g., via target_compile_definitions in CMake).
 *   - HAL_GFX_MAX_ROW_BUF defaults to HAL_GFX_DISPLAY_WIDTH and controls
 *     the internal row buffer used by HAL_GFX_Blit. If smaller than the
 *     display width, blits are chunked automatically.
 *
 * Minimum supported resolution:
 *   - Compile-time enforced minimum: 32x32 (practical minimum for primitives).
 *   - HAL_GFX_MAX_ROW_BUF must be >= 1.
 *
 * Initialization:
 *   - Call HAL_GFX_Init() after hal_display_init() so the GFX layer can
 *     query the backend for runtime width/height and set the clip region.
 *
 * Router contract expected by this layer:
 *   HAL_Display_DrawPixel(uint16_t x, uint16_t y, uint32_t color24);
 *   HAL_Display_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color24);
 *   HAL_Display_BlitRow(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len);
 */

#ifndef HAL_GFX_H
#define HAL_GFX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Configuration defaults (override at compile time)
 * -------------------------------------------------------------------------- */

#ifndef HAL_GFX_DISPLAY_WIDTH
#define HAL_GFX_DISPLAY_WIDTH  480
#endif

#ifndef HAL_GFX_DISPLAY_HEIGHT
#define HAL_GFX_DISPLAY_HEIGHT 320
#endif

#ifndef HAL_GFX_MAX_ROW_BUF
#define HAL_GFX_MAX_ROW_BUF HAL_GFX_DISPLAY_WIDTH
#endif

/* --------------------------------------------------------------------------
 * Compile-time sanity checks
 * -------------------------------------------------------------------------- */

/* Practical minimums */
#ifndef HAL_GFX_MIN_WIDTH
#define HAL_GFX_MIN_WIDTH 32
#endif

#ifndef HAL_GFX_MIN_HEIGHT
#define HAL_GFX_MIN_HEIGHT 32
#endif

#if (HAL_GFX_DISPLAY_WIDTH < HAL_GFX_MIN_WIDTH) || (HAL_GFX_DISPLAY_HEIGHT < HAL_GFX_MIN_HEIGHT)
#error "HAL_GFX_DISPLAY_WIDTH/HEIGHT too small. Increase to at least HAL_GFX_MIN_WIDTH x HAL_GFX_MIN_HEIGHT"
#endif

#if (HAL_GFX_MAX_ROW_BUF <= 0)
#error "HAL_GFX_MAX_ROW_BUF must be > 0"
#endif

/* --------------------------------------------------------------------------
 * Types
 * -------------------------------------------------------------------------- */

typedef uint16_t hal_gfx_color_t;   /* RGB565 */

typedef struct {
    int16_t x;
    int16_t y;
} hal_gfx_point_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;   /* width in pixels (count) */
    int16_t h;   /* height in pixels (count) */
} hal_gfx_rect_t;

/* --------------------------------------------------------------------------
 * Color helpers
 * -------------------------------------------------------------------------- */

/* Pack 8-bit RGB channels into RGB565 */
static inline hal_gfx_color_t HAL_GFX_ColorFromRGB(uint8_t r, uint8_t g, uint8_t b)
{
    return (hal_gfx_color_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Expand RGB565 to 24-bit 0xRRGGBB for backend/router.
 * Uses bit-replicate (fast, no division) rather than multiply+divide. */
static inline uint32_t HAL_GFX_ColorToBackend24(hal_gfx_color_t c)
{
    uint8_t r5 = (c >> 11) & 0x1F;
    uint8_t g6 = (c >> 5)  & 0x3F;
    uint8_t b5 = c & 0x1F;
    uint8_t r8 = (r5 << 3) | (r5 >> 2);
    uint8_t g8 = (g6 << 2) | (g6 >> 4);
    uint8_t b8 = (b5 << 3) | (b5 >> 2);
    return ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8;
}

/* --------------------------------------------------------------------------
 * Initialization
 * -------------------------------------------------------------------------- */

/**
 * Initialize the HAL GFX layer.
 * Must be called after hal_display_init() so the GFX layer can query the
 * backend for runtime width/height and set the initial clip rectangle.
 *
 * If the backend does not provide runtime size, compile-time defaults are used.
 */
void HAL_GFX_Init(void);

/* Optional helpers to query the active display size used by HAL_GFX */
int HAL_GFX_GetDisplayWidth(void);
int HAL_GFX_GetDisplayHeight(void);

/* --------------------------------------------------------------------------
 * Clipping API
 * -------------------------------------------------------------------------- */

void HAL_GFX_SetClip(int16_t x, int16_t y, int16_t w, int16_t h);
void HAL_GFX_ResetClip(void);
bool HAL_GFX_GetClip(hal_gfx_rect_t *clip);

/* --------------------------------------------------------------------------
 * Low-level pixel / fill (clipped)
 * -------------------------------------------------------------------------- */

void HAL_GFX_DrawPixel(int16_t x, int16_t y, hal_gfx_color_t color);
void HAL_GFX_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, hal_gfx_color_t color);

#define HAL_GFX_FillRegion HAL_GFX_FillRect   /* alias */

/* --------------------------------------------------------------------------
 * Lines
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
 * converts to backend 24-bit RGB and uses HAL_Display_BlitRow.
 * -------------------------------------------------------------------------- */

void HAL_GFX_Blit(const hal_gfx_color_t *src,
                  int16_t src_w, int16_t src_h,
                  int16_t dst_x, int16_t dst_y,
                  int16_t src_x, int16_t src_y,
                  int16_t src_w_clip, int16_t src_h_clip);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GFX_H */