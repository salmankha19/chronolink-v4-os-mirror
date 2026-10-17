# Fonts

No TTF is committed yet. When one is chosen, drop it here and run:

    python scripts/generate_font_ui_sans.py --ttf assets/fonts/<font>.ttf --out components/font/src/font_ui_sans.c

Recommended: Inter SemiBold (SIL Open Font License 1.1).

Record here when committed: font name, version, license, source URL.

Until then, use the built-in placeholder generator:

    python scripts/generate_font_ui_sans.py --builtin --out components/font/src/font_ui_sans.c

This produces an RGB565 font covering ASCII 0x20..0x7E. The built-in
mode converts the 8x8 mono glyph table, so 0x60..0x7E (lowercase) come
out as zero-width placeholder entries; the real TTF fills those in.
