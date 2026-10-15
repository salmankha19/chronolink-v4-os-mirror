/*
 * font_engine.c
 *
 * ChronoLink V4 OS — Font Engine Implementation
 *
 * Responsibilities:
 *   - Maintain a small registry of font_desc_t pointers.
 *   - Render 1-bit monochrome glyphs by calling HAL_GFX_DrawPixel.
 *   - Skip unknown characters (advance cursor by glyph width).
 *
 * Notes:
 *   - No heap allocation.
 *   - Bitmap must reside in flash (.rodata).
 *   - Linear search of the registry is acceptable for FE_MAX_FONTS <= 6.
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#include "font_engine.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "font_engine";

static font_desc_t s_fonts[FE_MAX_FONTS];
static bool        s_slot_used[FE_MAX_FONTS];

bool font_engine_register(const font_desc_t *desc)
{
    if (!desc) {
        ESP_LOGE(TAG, "Invalid font descriptor");
        return false;
    }
    if (desc->format == FONT_FORMAT_MONO_1BPP) {
        if (!desc->bitmap || desc->glyph_w == 0 || desc->glyph_h == 0) {
            ESP_LOGE(TAG, "Invalid MONO_1BPP font descriptor");
            return false;
        }
    } else if (desc->format == FONT_FORMAT_RGB565) {
        if (!desc->glyphs || !desc->pixels || desc->native_h == 0) {
            ESP_LOGE(TAG, "Invalid RGB565 font descriptor");
            return false;
        }
    } else {
        ESP_LOGE(TAG, "Unknown font format %d", (int)desc->format);
        return false;
    }

    for (int i = 0; i < FE_MAX_FONTS; i++) {
        if (!s_slot_used[i]) {
            s_fonts[i] = *desc;
            s_slot_used[i] = true;
            if (desc->format == FONT_FORMAT_MONO_1BPP) {
                ESP_LOGI(TAG, "Registered MONO font id=%d %dx%d range %d..%d",
                         desc->id, desc->glyph_w, desc->glyph_h,
                         desc->first_char, desc->last_char);
            } else {
                ESP_LOGI(TAG, "Registered RGB565 font id=%d native_h=%d range %d..%d",
                         desc->id, desc->native_h,
                         desc->first_char, desc->last_char);
            }
            return true;
        }
    }

    ESP_LOGW(TAG, "Font registry full (max=%d)", FE_MAX_FONTS);
    return false;
}

void font_engine_draw_text(int16_t x, int16_t y, hal_gfx_color_t color,
                           uint8_t font_id, const char *text)
{
    if (!text) return;

    const font_desc_t *f = NULL;
    for (int i = 0; i < FE_MAX_FONTS; i++) {
        if (s_slot_used[i] && s_fonts[i].id == font_id) {
            f = &s_fonts[i];
            break;
        }
    }
    if (!f) {
        ESP_LOGW(TAG, "Font id=%d not found", font_id);
        return;
    }

    const uint8_t w             = f->glyph_w;
    const uint8_t h             = f->glyph_h;
    const uint8_t bytes_per_row = w / 8;
    const uint8_t bytes_per_glyph = bytes_per_row * h;

    int16_t cursor_x = x;

    for (const char *p = text; *p; ++p) {
        uint8_t ch = (uint8_t)*p;
        if (ch < f->first_char || ch > f->last_char) {
            cursor_x += w;
            continue;
        }

        uint8_t idx   = ch - f->first_char;
        const uint8_t *glyph = f->bitmap + (idx * bytes_per_glyph);

        for (uint8_t row = 0; row < h; row++) {
            for (uint8_t byte_col = 0; byte_col < bytes_per_row; byte_col++) {
                uint8_t b = glyph[row * bytes_per_row + byte_col];
                for (uint8_t bit = 0; bit < 8; bit++) {
                    if (b & (0x80 >> bit)) {
                        int16_t px = cursor_x + (byte_col * 8) + bit;
                        int16_t py = y + row;
                        HAL_GFX_DrawPixel(px, py, color);
                    }
                }
            }
        }
        cursor_x += w;
    }
}

void font_engine_draw_text_scaled(int16_t x, int16_t y,
                                  uint8_t font_id,
                                  uint16_t target_h,
                                  const char *text,
                                  int16_t *out_end_x)
{
    if (out_end_x) *out_end_x = x;
    if (!text) return;

    const font_desc_t *f = NULL;
    for (int i = 0; i < FE_MAX_FONTS; i++) {
        if (s_slot_used[i] && s_fonts[i].id == font_id) {
            f = &s_fonts[i]; break;
        }
    }
    if (!f || f->format != FONT_FORMAT_RGB565 || !f->glyphs || !f->pixels) {
        ESP_LOGW(TAG, "draw_text_scaled: font id=%d not RGB565", font_id);
        return;
    }
    if (target_h == 0) target_h = f->native_h;
    if (target_h == 0) return;

    int16_t cursor_x = x;
    for (const char *p = text; *p; ++p) {
        uint8_t ch = (uint8_t)*p;
        if (ch < f->first_char || ch > f->last_char) {
            cursor_x += target_h / 2; continue;
        }
        const font_glyph_t *g = &f->glyphs[ch - f->first_char];
        if (g->w == 0) { cursor_x += target_h / 2; continue; }

        int16_t dst_w = (int16_t)(((uint32_t)g->w * target_h) / f->native_h);
        if (dst_w <= 0) dst_w = 1;

        HAL_GFX_BlitScaled(&f->pixels[g->offset],
                           (int16_t)g->w, (int16_t)f->native_h,
                           cursor_x, y,
                           dst_w, (int16_t)target_h);
        cursor_x += dst_w;
    }
    if (out_end_x) *out_end_x = cursor_x;
}
