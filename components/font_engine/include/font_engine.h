/*
 * font_engine.h
 *
 * ChronoLink V4 OS — Font Engine
 *
 * Generic bitmap font renderer.  Producers register font descriptors at
 * runtime; the engine renders 1-bit flash-resident glyphs via HAL_GFX.
 *
 * Design:
 *   - Zero font data inside the engine.
 *   - Up to FE_MAX_FONTS slots.
 *   - Glyph width must be a multiple of 8.
 *   - All rendering is clipped by the active HAL_GFX clip rect.
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hal_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FE_MAX_FONTS 4

/**
 * @brief Font descriptor.
 *
 * Bitmap layout: 1 bit per pixel, row-major, MSB-first.
 * Each glyph consumes (glyph_w / 8) * glyph_h bytes.
 */
typedef struct {
    uint8_t  id;            /**< Font ID referenced by DM_CMD_DRAW_TEXT */
    uint8_t  glyph_w;       /**< Glyph width in pixels (multiple of 8) */
    uint8_t  glyph_h;       /**< Glyph height in pixels */
    uint8_t  first_char;    /**< ASCII / code-page start */
    uint8_t  last_char;     /**< ASCII / code-page end (inclusive) */
    const uint8_t *bitmap;  /**< Flash-resident 1-bit bitmap table */
} font_desc_t;

/**
 * @brief Register a font descriptor.
 * @param desc Pointer to flash-resident descriptor.
 * @return true on success, false if registry is full or descriptor invalid.
 */
bool font_engine_register(const font_desc_t *desc);

/**
 * @brief Draw a null-terminated string using the specified font.
 * @param x      Start X
 * @param y      Start Y (top of glyph)
 * @param color  RGB565 color
 * @param font_id Registered font ID
 * @param text   Null-terminated string
 */
void font_engine_draw_text(int16_t x, int16_t y, hal_gfx_color_t color,
                           uint8_t font_id, const char *text);

#ifdef __cplusplus
}
#endif
