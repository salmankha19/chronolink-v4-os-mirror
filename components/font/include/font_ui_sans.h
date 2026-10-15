#pragma once
#include "font_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scalable UI sans-serif font, id = 1.
   PLACEHOLDER glyph set: '0','1','2',':' at 8x8 native, RGB565.
   Replace with the real asset by running:
     python scripts/generate_font_ui_sans.py \
         --font assets/fonts/Inter-SemiBold.ttf \
         --out  components/font/src/font_ui_sans.c */
extern const font_desc_t FONT_UI_SANS;

#ifdef __cplusplus
}
#endif
