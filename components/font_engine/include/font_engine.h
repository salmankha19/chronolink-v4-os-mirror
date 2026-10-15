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

#define FE_MAX_FONTS 6

typedef enum {
    FONT_FORMAT_MONO_1BPP = 0,   /* 1-bit glyphs, 1-bit panels (MAX7219, dot-matrix) */
    FONT_FORMAT_RGB565    = 1,   /* RGB565 glyphs, colour panels (ST7796S)           */
} font_format_t;

/* Advisory script tag. The engine does not use this field in the
   current revision. Future callers (theme registry, producers) use
   it to select the correct font for a string by script rather than
   hard-coding font IDs. Arabic and Urdu fonts will register with
   FONT_SCRIPT_ARABIC and pre-shaped glyph sequences; RTL rendering
   will be a separate function, not a parameter to the LTR path. */
typedef enum {
    FONT_SCRIPT_LATIN  = 0,
    FONT_SCRIPT_ARABIC = 1,   /* covers Arabic and Urdu codepoints */
} font_script_t;

/* Per-glyph entry in an RGB565 font. Width varies per character;
   height is uniform (native_h) across the font. */
typedef struct {
    uint16_t w;
    uint32_t offset;  /* start index into pixels[], RGB565 units */
} font_glyph_t;

/* A registered font. Two formats coexist: MONO_1BPP for 1-bit panels
   (MAX7219, dot-matrix) and RGB565 for colour panels (ST7796S).
   Neither supersedes the other. `format` selects which field group
   is meaningful; the other group must be zero. */
typedef struct {
    uint8_t  id;
    uint8_t  first_char;
    uint8_t  last_char;
    font_format_t format;
    font_script_t script;

    /* FONT_FORMAT_MONO_1BPP: fixed-size glyphs, per-pixel draw */
    uint8_t  glyph_w;
    uint8_t  glyph_h;
    const uint8_t *bitmap;

    /* FONT_FORMAT_RGB565: per-glyph widths, uniform native height */
    uint16_t native_h;
    const font_glyph_t *glyphs;
    const uint16_t *pixels;
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

/* Render text with a scalable RGB565 font. The font's native_h is
   scaled to target_h; glyph widths scale proportionally so relative
   spacing is preserved. target_h == 0 means "use native_h".
   Unknown glyphs advance by target_h / 2 without drawing.
   Callers that need the post-draw cursor pass out_end_x; others NULL.
   No-op if font_id is unregistered or the font is not RGB565. */
void font_engine_draw_text_scaled(int16_t x, int16_t y,
                                  uint8_t font_id,
                                  uint16_t target_h,
                                  const char *text,
                                  int16_t *out_end_x);

#ifdef __cplusplus
}
#endif
