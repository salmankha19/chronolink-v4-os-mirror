# Fonts

No TTF is committed yet. When one is chosen, drop it here and run:

    python scripts/generate_font_ui_sans.py \
        --font assets/fonts/<font>.ttf \
        --out  components/font/src/font_ui_sans.c

Recommended: Inter SemiBold (SIL Open Font License 1.1).

Record here when committed: font name, version, license, source URL.

Until then, components/font/src/font_ui_sans.c is a hand-authored
placeholder covering '0','1','2',':' at 8x8 RGB565, sufficient to
exercise the pipeline end to end.
