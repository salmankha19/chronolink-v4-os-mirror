/*
 * font_builtin_8x8.h
 *
 * ChronoLink V4 OS — Built-in 8×8 Monospace Font
 *
 * Coverage: ASCII 0x20 (space) through 0x5F (underscore).
 * Storage:  64 glyphs × 8 bytes = 512 bytes in flash.
 *
 * Usage:
 *   extern const font_desc_t FONT_BUILTIN_8X8;
 *   font_engine_register(&FONT_BUILTIN_8X8);
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#pragma once
#include "font_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Built-in 8×8 font descriptor (id = 0). */
extern const font_desc_t FONT_BUILTIN_8X8;

#ifdef __cplusplus
}
#endif
