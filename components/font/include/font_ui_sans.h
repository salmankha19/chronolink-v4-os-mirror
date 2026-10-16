#pragma once
#include "font_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scalable UI sans-serif font, id = 1 (FONT_FORMAT_RGB565).

   Two generation modes, both via scripts/generate_font_ui_sans.py:

     Built-in placeholder (ASCII 0x20..0x7E, converted from the 8x8
     mono table; lowercase 0x60..0x7E are zero-width placeholders):

       python scripts/generate_font_ui_sans.py --builtin \
           --out components/font/src/font_ui_sans.c

     Real TrueType font (requires Pillow):

       python scripts/generate_font_ui_sans.py \
           --ttf assets/fonts/Inter-SemiBold.ttf \
           --out components/font/src/font_ui_sans.c

   Recommended face: Inter SemiBold (SIL Open Font License 1.1). */
extern const font_desc_t FONT_UI_SANS;

#ifdef __cplusplus
}
#endif
