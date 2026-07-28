// hal_gfx.c
// ChronoLink V4 — Graphics primitives layer (parameterized resolution)
// Assumes router signatures:
//   HAL_Display_DrawPixel(uint16_t x, uint16_t y, uint32_t color24)
//   HAL_Display_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color24)
//   HAL_Display_BlitRow(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len)
// Color conversion helper: HAL_GFX_ColorToBackend24(hal_gfx_color_t) -> uint32_t (0xRRGGBB)

#include "hal_gfx.h"
#include "hal_display.h"
#include <stdint.h>
#include <string.h>

/* Static clip state (initialized at runtime in HAL_GFX_Init) */
static hal_gfx_rect_t s_clip;

/* Active display size used by HAL_GFX (set in HAL_GFX_Init) */
static int s_disp_w = HAL_GFX_DISPLAY_WIDTH;
static int s_disp_h = HAL_GFX_DISPLAY_HEIGHT;

/* Row buffer of uint32_t pixels (0xRRGGBB) to match router/backends */
static uint32_t s_row_buf[HAL_GFX_MAX_ROW_BUF];

/* Helpers */
static inline int16_t gfx_abs_i16(int16_t v) { return v < 0 ? -v : v; }

static inline bool rect_intersect(int16_t x, int16_t y, int16_t w, int16_t h,
                                  hal_gfx_rect_t *out)
{
    if (w <= 0 || h <= 0) return false;

    int16_t x1 = x;
    int16_t y1 = y;
    int16_t x2 = x + w - 1;
    int16_t y2 = y + h - 1;

    int16_t cx1 = s_clip.x;
    int16_t cy1 = s_clip.y;
    int16_t cx2 = s_clip.x + s_clip.w - 1;
    int16_t cy2 = s_clip.y + s_clip.h - 1;

    if (x1 < cx1) x1 = cx1;
    if (y1 < cy1) y1 = cy1;
    if (x2 > cx2) x2 = cx2;
    if (y2 > cy2) y2 = cy2;

    if (x1 > x2 || y1 > y2) return false;

    out->x = x1;
    out->y = y1;
    out->w = x2 - x1 + 1;
    out->h = y2 - y1 + 1;
    return true;
}

/* --------------------------------------------------------------------------
 * Initialization
 * -------------------------------------------------------------------------- */

void HAL_GFX_Init(void)
{
    /* Query backend for runtime size if available */
#ifdef HAL_Display_GetWidth
    int w = HAL_Display_GetWidth();
#else
    int w = -1;
#endif

#ifdef HAL_Display_GetHeight
    int h = HAL_Display_GetHeight();
#else
    int h = -1;
#endif

    if (w > 0) s_disp_w = w;
    else s_disp_w = HAL_GFX_DISPLAY_WIDTH;

    if (h > 0) s_disp_h = h;
    else s_disp_h = HAL_GFX_DISPLAY_HEIGHT;

    /* Enforce minimums at runtime as a safety net */
    if (s_disp_w < HAL_GFX_MIN_WIDTH) s_disp_w = HAL_GFX_MIN_WIDTH;
    if (s_disp_h < HAL_GFX_MIN_HEIGHT) s_disp_h = HAL_GFX_MIN_HEIGHT;

    s_clip.x = 0;
    s_clip.y = 0;
    s_clip.w = (int16_t)s_disp_w;
    s_clip.h = (int16_t)s_disp_h;
}

int HAL_GFX_GetDisplayWidth(void) { return s_disp_w; }
int HAL_GFX_GetDisplayHeight(void) { return s_disp_h; }

/* --------------------------------------------------------------------------
 * Public clip API
 * -------------------------------------------------------------------------- */

void HAL_GFX_SetClip(int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    s_clip.x = x;
    s_clip.y = y;
    s_clip.w = w;
    s_clip.h = h;
}

void HAL_GFX_ResetClip(void)
{
    s_clip.x = 0;
    s_clip.y = 0;
    s_clip.w = (int16_t)s_disp_w;
    s_clip.h = (int16_t)s_disp_h;
}

bool HAL_GFX_GetClip(hal_gfx_rect_t *clip)
{
    if (!clip) return false;
    *clip = s_clip;
    return true;
}

/* --------------------------------------------------------------------------
 * Pixel / Fill
 * -------------------------------------------------------------------------- */

void HAL_GFX_DrawPixel(int16_t x, int16_t y, hal_gfx_color_t color)
{
    if (x < s_clip.x || y < s_clip.y) return;
    if (x >= s_clip.x + s_clip.w || y >= s_clip.y + s_clip.h) return;
    uint32_t c24 = HAL_GFX_ColorToBackend24(color);
    HAL_Display_DrawPixel((uint16_t)x, (uint16_t)y, c24);
}

void HAL_GFX_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, hal_gfx_color_t color)
{
    hal_gfx_rect_t cr;
    if (!rect_intersect(x, y, w, h, &cr)) return;
    uint32_t c24 = HAL_GFX_ColorToBackend24(color);
    HAL_Display_FillRect((uint16_t)cr.x, (uint16_t)cr.y, (uint16_t)cr.w, (uint16_t)cr.h, c24);
}

/* Fast H/V lines */
void HAL_GFX_DrawHLine(int16_t x, int16_t y, int16_t w, hal_gfx_color_t color)
{
    if (w <= 0) return;
    hal_gfx_rect_t cr;
    if (!rect_intersect(x, y, w, 1, &cr)) return;
    uint32_t c24 = HAL_GFX_ColorToBackend24(color);
    HAL_Display_FillRect((uint16_t)cr.x, (uint16_t)cr.y, (uint16_t)cr.w, 1, c24);
}

void HAL_GFX_DrawVLine(int16_t x, int16_t y, int16_t h, hal_gfx_color_t color)
{
    if (h <= 0) return;
    hal_gfx_rect_t cr;
    if (!rect_intersect(x, y, 1, h, &cr)) return;
    uint32_t c24 = HAL_GFX_ColorToBackend24(color);
    HAL_Display_FillRect((uint16_t)cr.x, (uint16_t)cr.y, 1, (uint16_t)cr.h, c24);
}

/* Bresenham for general lines (per-pixel clipped) */
void HAL_GFX_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, hal_gfx_color_t color)
{
    if (y0 == y1) {
        if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
        HAL_GFX_DrawHLine(x0, y0, x1 - x0 + 1, color);
        return;
    }
    if (x0 == x1) {
        if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
        HAL_GFX_DrawVLine(x0, y0, y1 - y0 + 1, color);
        return;
    }

    uint32_t c24 = HAL_GFX_ColorToBackend24(color);
    int16_t dx = gfx_abs_i16(x1 - x0);
    int16_t dy = gfx_abs_i16(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (dx > dy ? dx : -dy) / 2;

    for (;;) {
        if (x0 >= s_clip.x && y0 >= s_clip.y && x0 < s_clip.x + s_clip.w && y0 < s_clip.y + s_clip.h) {
            HAL_Display_DrawPixel((uint16_t)x0, (uint16_t)y0, c24);
        }
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 <  dy) { err += dx; y0 += sy; }
    }
}

/* Rectangles */
void HAL_GFX_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, hal_gfx_color_t color)
{
    if (w <= 0 || h <= 0) return;
    HAL_GFX_DrawHLine(x, y, w, color);
    HAL_GFX_DrawHLine(x, y + h - 1, w, color);
    HAL_GFX_DrawVLine(x, y + 1, h - 2, color);
    HAL_GFX_DrawVLine(x + w - 1, y + 1, h - 2, color);
}

/* Circles and fills using spans */
void HAL_GFX_DrawCircle(int16_t cx, int16_t cy, int16_t r, hal_gfx_color_t color)
{
    if (r < 0) return;
    if (r == 0) { HAL_GFX_DrawPixel(cx, cy, color); return; }

    uint32_t c24 = HAL_GFX_ColorToBackend24(color);
    int16_t x = 0, y = r;
    int16_t d = 3 - 2 * r;

    while (x <= y) {
        const int16_t pts[8][2] = {
            { cx + x, cy + y }, { cx - x, cy + y },
            { cx + x, cy - y }, { cx - x, cy - y },
            { cx + y, cy + x }, { cx - y, cy + x },
            { cx + y, cy - x }, { cx - y, cy - x }
        };
        for (int i = 0; i < 8; ++i) {
            int16_t px = pts[i][0], py = pts[i][1];
            if (px >= s_clip.x && py >= s_clip.y && px < s_clip.x + s_clip.w && py < s_clip.y + s_clip.h) {
                HAL_Display_DrawPixel((uint16_t)px, (uint16_t)py, c24);
            }
        }
        x++;
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
    }
}

void HAL_GFX_FillCircle(int16_t cx, int16_t cy, int16_t r, hal_gfx_color_t color)
{
    if (r < 0) return;
    if (r == 0) { HAL_GFX_DrawPixel(cx, cy, color); return; }

    int16_t x = 0, y = r;
    int16_t d = 3 - 2 * r;

    while (x <= y) {
        HAL_GFX_DrawHLine(cx - x, cy - y, 2 * x + 1, color);
        HAL_GFX_DrawHLine(cx - x, cy + y, 2 * x + 1, color);
        HAL_GFX_DrawHLine(cx - y, cy - x, 2 * y + 1, color);
        HAL_GFX_DrawHLine(cx - y, cy + x, 2 * y + 1, color);

        x++;
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
    }
}

/* Rounded rect helpers (outline and fill) */
static void gfx_draw_circle_quarter(int16_t cx, int16_t cy, int16_t r, hal_gfx_color_t color, int8_t qx, int8_t qy)
{
    if (r <= 0) return;
    uint32_t c24 = HAL_GFX_ColorToBackend24(color);
    int16_t x = 0, y = r;
    int16_t d = 3 - 2 * r;

    while (x <= y) {
        int16_t pts[4][2];
        int cnt = 0;
        if (qx > 0 && qy > 0) { pts[cnt][0] = cx + x; pts[cnt++][1] = cy + y; pts[cnt][0] = cx + y; pts[cnt++][1] = cy + x; }
        else if (qx < 0 && qy > 0) { pts[cnt][0] = cx - x; pts[cnt++][1] = cy + y; pts[cnt][0] = cx - y; pts[cnt++][1] = cy + x; }
        else if (qx > 0 && qy < 0) { pts[cnt][0] = cx + x; pts[cnt++][1] = cy - y; pts[cnt][0] = cx + y; pts[cnt++][1] = cy - x; }
        else { pts[cnt][0] = cx - x; pts[cnt++][1] = cy - y; pts[cnt][0] = cx - y; pts[cnt++][1] = cy - x; }

        for (int i = 0; i < cnt; ++i) {
            int16_t px = pts[i][0], py = pts[i][1];
            if (px >= s_clip.x && py >= s_clip.y && px < s_clip.x + s_clip.w && py < s_clip.y + s_clip.h) {
                HAL_Display_DrawPixel((uint16_t)px, (uint16_t)py, c24);
            }
        }

        x++;
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
    }
}

static void gfx_fill_circle_quarter(int16_t cx, int16_t cy, int16_t r, hal_gfx_color_t color, int8_t qx, int8_t qy)
{
    if (r <= 0) return;
    int16_t x = 0, y = r;
    int16_t d = 3 - 2 * r;

    while (x <= y) {
        if (qy > 0) {
            if (qx > 0) { HAL_GFX_DrawHLine(cx,     cy + y, x + 1, color); HAL_GFX_DrawHLine(cx,     cy + x, y + 1, color); }
            else        { HAL_GFX_DrawHLine(cx - x, cy + y, x + 1, color); HAL_GFX_DrawHLine(cx - y, cy + x, y + 1, color); }
        } else {
            if (qx > 0) { HAL_GFX_DrawHLine(cx,     cy - y, x + 1, color); HAL_GFX_DrawHLine(cx,     cy - x, y + 1, color); }
            else        { HAL_GFX_DrawHLine(cx - x, cy - y, x + 1, color); HAL_GFX_DrawHLine(cx - y, cy - x, y + 1, color); }
        }

        x++;
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
    }
}

void HAL_GFX_DrawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, hal_gfx_color_t color)
{
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    int16_t max_r = (w < h) ? (w / 2) : (h / 2);
    if (r > max_r) r = max_r;
    if (r == 0) { HAL_GFX_DrawRect(x, y, w, h, color); return; }

    HAL_GFX_DrawHLine(x + r,     y,         w - 2 * r, color);
    HAL_GFX_DrawHLine(x + r,     y + h - 1, w - 2 * r, color);
    HAL_GFX_DrawVLine(x,         y + r,     h - 2 * r, color);
    HAL_GFX_DrawVLine(x + w - 1, y + r,     h - 2 * r, color);

    gfx_draw_circle_quarter(x + r,         y + r,         r, color, -1, -1);
    gfx_draw_circle_quarter(x + w - r - 1, y + r,         r, color, +1, -1);
    gfx_draw_circle_quarter(x + r,         y + h - r - 1, r, color, -1, +1);
    gfx_draw_circle_quarter(x + w - r - 1, y + h - r - 1, r, color, +1, +1);
}

void HAL_GFX_FillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, hal_gfx_color_t color)
{
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    int16_t max_r = (w < h) ? (w / 2) : (h / 2);
    if (r > max_r) r = max_r;
    if (r == 0) { HAL_GFX_FillRect(x, y, w, h, color); return; }

    HAL_GFX_FillRect(x + r, y, w - 2 * r, h, color);
    HAL_GFX_FillRect(x,         y + r, r, h - 2 * r, color);
    HAL_GFX_FillRect(x + w - r, y + r, r, h - 2 * r, color);

    gfx_fill_circle_quarter(x + r,         y + r,         r, color, -1, -1);
    gfx_fill_circle_quarter(x + w - r - 1, y + r,         r, color, +1, -1);
    gfx_fill_circle_quarter(x + r,         y + h - r - 1, r, color, -1, +1);
    gfx_fill_circle_quarter(x + w - r - 1, y + h - r - 1, r, color, +1, +1);
}

/* Blit: convert source RGB565 -> uint32_t RGB24 into s_row_buf and call router BlitRow */
void HAL_GFX_Blit(const hal_gfx_color_t *src,
                  int16_t src_w, int16_t src_h,
                  int16_t dst_x, int16_t dst_y,
                  int16_t src_x, int16_t src_y,
                  int16_t src_w_clip, int16_t src_h_clip)
{
    if (!src) return;
    if (src_w <= 0 || src_h <= 0) return;
    if (src_w_clip <= 0 || src_h_clip <= 0) return;

    /* Clip source to bitmap bounds */
    if (src_x < 0) { dst_x -= src_x; src_w_clip += src_x; src_x = 0; }
    if (src_y < 0) { dst_y -= src_y; src_h_clip += src_y; src_y = 0; }
    if (src_x + src_w_clip > src_w) src_w_clip = src_w - src_x;
    if (src_y + src_h_clip > src_h) src_h_clip = src_h - src_y;
    if (src_w_clip <= 0 || src_h_clip <= 0) return;

    /* Clip destination to screen clip */
    int16_t clip_x1 = s_clip.x;
    int16_t clip_y1 = s_clip.y;
    int16_t clip_x2 = s_clip.x + s_clip.w - 1;
    int16_t clip_y2 = s_clip.y + s_clip.h - 1;

    int16_t dst_x1 = dst_x;
    int16_t dst_y1 = dst_y;
    int16_t dst_x2 = dst_x + src_w_clip - 1;
    int16_t dst_y2 = dst_y + src_h_clip - 1;

    if (dst_x1 < clip_x1) { int16_t shift = clip_x1 - dst_x1; src_x += shift; dst_x1 = clip_x1; }
    if (dst_y1 < clip_y1) { int16_t shift = clip_y1 - dst_y1; src_y += shift; dst_y1 = clip_y1; }
    if (dst_x2 > clip_x2) dst_x2 = clip_x2;
    if (dst_y2 > clip_y2) dst_y2 = clip_y2;

    int16_t out_w = dst_x2 - dst_x1 + 1;
    int16_t out_h = dst_y2 - dst_y1 + 1;
    if (out_w <= 0 || out_h <= 0) return;

    for (int16_t row = 0; row < out_h; ++row) {
        const hal_gfx_color_t *src_row = src + (src_y + row) * src_w + src_x;
        int16_t remaining = out_w;
        int16_t col_offset = 0;
        int16_t blit_x = dst_x1;

        while (remaining > 0) {
            int16_t chunk = remaining > HAL_GFX_MAX_ROW_BUF ? HAL_GFX_MAX_ROW_BUF : remaining;

            for (int16_t i = 0; i < chunk; ++i) {
                s_row_buf[i] = HAL_GFX_ColorToBackend24(src_row[col_offset + i]);
            }

            HAL_Display_BlitRow((uint16_t)blit_x, (uint16_t)(dst_y1 + row), s_row_buf, (uint16_t)chunk);

            remaining -= chunk;
            col_offset += chunk;
            blit_x += chunk;
        }
    }
}