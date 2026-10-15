# PR 8 pre-flight: scalable time face on RGB565 panels

Status: analysis complete, ready for implementation.
Reviewed at main = 94cb534 (2026-10-15).

## 0. Primary-source facts

| Fact | Evidence |
|---|---|
| Current face is `"%02u:%02u:%02u"` at (128, 232), font_id 0, RGB565 white on black | `ui_producer.c` |
| Built-in font is 8x8, covers ASCII 0x20..0x5F (no lowercase) | `font_builtin_8x8.c` |
| 8 px glyph is ~0.055" on the 4" 320x480 panel (144 PPI) | Derived |
| `font_engine_draw_text()` calls `HAL_GFX_DrawPixel` per lit pixel | `font_engine.c` |
| `HAL_Display_ST7796S_DrawPixel` = CASET + RASET + RAMWR + 2 data bytes | `hal_display_st7796s.c` |
| ~2,700 lit pixels at 48x64 -> ~280 ms/glyph at 20 MHz | Derived |
| `font_engine` registry `FE_MAX_FONTS = 4`; only id 0 registered | `font_engine.h`; `main.c` |
| `font_desc_t` carries only mono fields today | `font_engine.h` |
| `font` component holds `font_builtin_8x8.c`; `REQUIRES font_engine` | `components/font/CMakeLists.txt` |
| `display_manager` routes `DM_CMD_DRAW_TEXT` -> `font_engine_draw_text()`, `DM_CMD_BLIT_ICON` -> `HAL_GFX_Blit` | `display_manager.c` |
| `HAL_GFX_Blit` uses row-blit path (RGB565->RGB24 -> `HAL_Display_BlitRow`) | `hal_gfx.c` |
| `HAL_Display_ST7796S_BlitRow` sets address window once per row | `hal_display_st7796s.c` |
| Panel is 320x480 portrait by Kconfig default | `hal_display_st7796s.h`; `sdkconfig.single_mcu` |
| Frame path proven at 1 Hz dispatch, no drops (PR 6, 5044dfe) | `docs/STATUS.md` |
| `DM_MAX_CMDS_PER_FRAME 16`, `DM_MAX_TEXT_LEN 16`, `DM_MAX_ICONS 16` | `display_manager.h` |
| ST7796S backend declines `HAL_CAP_TEXT`; font layer is sanctioned text path | `hal_display_st7796s.c`; `hal_display.h` |
| MAX7219 backend exists, every entry point returns `HAL_ERR_DEV` | `hal_display_max7219.c` |
| MAX7219 gated behind `CHRONOLINK_HAL_ALLOW_STUB_BACKENDS`, currently off | `chronolink_hal/Kconfig`; `sdkconfig.single_mcu` |
| ROADMAP Phase 1 goal: "readable from across the room" | `docs/ROADMAP.md` |

## 1. Problem

The 8x8 font cannot grow for the ST7796S panel. Two reasons:

1. Per-pixel draw cost. `font_engine_draw_text()` calls `HAL_GFX_DrawPixel`
   per lit pixel. At 150 px tall, four digits plus colon ~1.5 s per frame,
   blocking core1_task.
2. Format mismatch. RGB565 panels want RGB565 glyphs and a row-blit path.
   The 1-bit representation in `font_desc_t` cannot carry colour or scale.

Fix: add a second glyph format in the registry, and a scaling primitive
in HAL_GFX.

## 2. Architecture decision

Two facts fix the shape:
- `font_engine` is a runtime registry; the `font` component owns assets.
  Digits belong there, not in `ui_producer`.
- `HAL_GFX_BlitScaled` is a graphics primitive; belongs next to
  `HAL_GFX_Blit` in `chronolink_hal`.

Rejected alternatives:
- Pre-rendered bitmaps owned by `ui_producer`: bypasses font_engine and
  the font asset layer, contradicts FE_MAX_FONTS intent and
  Architecture.md layer rules.
- 7-segment via DM_CMD_FILL_RECT: fast but commits the aesthetic;
  reference is modern sans.
- Larger 1-bit font through existing per-pixel path: the ~280 ms wall.
- Separate UI renderer above display_manager: duplicates registry, adds a
  second path to display, sits above the frame pipeline.

Chosen: extend `font_engine` with `FONT_FORMAT_RGB565`, add
`HAL_GFX_BlitScaled` as a HAL primitive. Existing MONO_1BPP path
untouched.

## 3. Dual-format rationale

Both formats are first-class. Neither supersedes the other.

| Format | Panels | Path | Reason |
|---|---|---|---|
| MONO_1BPP | MAX7219, dot-matrix, any 1-bit grid | per-pixel draw | Native 8x8 grid, no colour, no scaling needed |
| RGB565 | ST7796S, future colour panels | row-blit via BlitScaled | Scaled glyphs, RGB565, row DMA |

Consequences:
- `FONT_BUILTIN_8X8` is not deprecated. It is the reference font for
  1-bit panels. When MAX7219 is un-stubbed, DM_CMD_DRAW_TEXT is ready.
- `font_desc_t` becomes a discriminated struct; `format` selects which
  field group is meaningful.
- The `font` component gains two asset pipelines: RGB565 (this PR) and
  a future 1-bit generator or hand-authored set.
- Producers select format by panel, not by which fonts are registered.
- docs/hal.md and docs/hil.md describe both paths side by side.

## 4. Frame shape

A time frame is two commands:
1. DM_CMD_DRAW_TEXT_SCALED for "H:MM"
2. DM_CMD_DRAW_TEXT_SCALED for "AM" or "PM"

DM_MAX_CMDS_PER_FRAME stays 16. Union grows by zero bytes because
text_scaled overlays text.

First frame prepends DM_CMD_CLEAR black once. Later frames do not clear;
each scaled glyph bitmap carries its own background and overwrites its
rectangle.

## 5. Font asset

- Source: TTF supplied by the project, dropped in assets/fonts/.
  Chosen: Inter SemiBold (SIL OFL). Placeholder ships in this PR.
- Generator: scripts/generate_font_ui_sans.py
  - Inputs: --font <path>, --out <c-file>, --numerals, --sizes, --check-coverage
  - Outputs: FONT_UI_SANS (id 1) + glyph tables + flat uint16_t RGB565
  - Committed so build is hermetic.
- Numerals: Western only in PR 8. Arabic-Indic (U+0660..0669) and Urdu
  (U+06F0..06F9) will be a generator flag, not a code branch, when needed.
- Size classes: one native (192 px, downscale-only) in PR 8.
- Coverage check: --check-coverage prints presence of required glyphs.
- Placeholder: '0','1','2',':' at 8x8 RGB565, so the pipeline compiles
  and runs before the real TTF arrives.

## 6. Layout

Derived from HAL_Display_GetWidth/Height() at boot.

Portrait (W < H):
- glyph_h = H * 5 / 16 -> 150 px on 480
- glyph_w = glyph_h * 2 / 3 -> 100 px
- badge height = glyph_h / 3, badge width = badge_h * 2 / 3
- cluster centred horizontally; y0 = (H - glyph_h) / 2
- badge baseline-aligned to cluster right edge

Landscape (W > H):
- glyph_w = glyph_h * 4 / 5, layout re-centres

At 320x480 cluster ~470 px wide, ~35 px margins.

## 7. Performance

320x480, glyph_h = 150, 1 Hz:

| Step | Cost |
|---|---|
| 4 scaled digit blits | ~12 ms |
| colon + badge | ~3 ms |
| Per frame | ~15 ms -> ~1.5% of core 1 |

Scales with pixel count. 480x800 would be ~3%.

## 8. Risks

1. `font_desc_t` size change: existing designated initialisers must still
   compile. FONT_BUILTIN_8X8 uses them today; confirm at build time.
2. `HAL_GFX_BlitScaled` must intersect destination with s_clip as
   `HAL_GFX_Blit` does.
3. Nearest-neighbour at non-integer ratios: mitigated by picking native
   size closest to target_h and always downscaling.
4. Font license: recorded in assets/fonts/README.md.
5. First-frame clear: keep DM_CMD_CLEAR on frame 1 only.
6. `HAL_Display_Show()` is a no-op on ST7796S; no swap needed.
7. Verify `HAL_Display_BlitRow` does not gate on HAL_CAP_BITMAP in the
   router (it does not today; confirm).
8. Do not add per-frame logging.

## 9. Verification without a panel

Devkit path is no-op returning HAL_OK (see pr6-preflight.md §5).

Log evidence:
1. `font_engine: Registered font id=1 ...`
2. `ui_producer: time valid, producing`
3. `display_manager: frame dispatched [n=2]` at 1 Hz
4. No font-not-found, no queue-full, no submit failed

Panel photo is the only additional evidence once wired.

## 10. Docs and STATUS updates in the same PR

- STATUS.md: bump reviewed hash; add "Scalable time face (PR 8)";
  clarify the "ST7796S write_text" row.
- ROADMAP.md: Phase 1 clock face -> in progress.
- Architecture.md: one paragraph naming the two panel families and
  which font format each uses.
- hal.md §6: extend font module description.
- hil.md: companion sentence for 1-bit path.
- pr8-preflight.md: this document.

## 11. Commit plan

```text
pr8/scalable-font-time-face

  chronolink_hal:  add HAL_GFX_BlitScaled primitive
  font_engine:     FONT_FORMAT_RGB565 + draw_text_scaled() + script tag
  display_manager: add DM_CMD_DRAW_TEXT_SCALED
  font:            FONT_UI_SANS placeholder (RGB565, 8x8)
  ui_producer:     render time via scalable font
  docs:            STATUS.md / ROADMAP.md / Architecture.md / hal.md
```

## 12. Future scripts (Arabic, Urdu)

Arabic and Urdu are planned.

- font_script_t tag on font_desc_t (advisory, unused by the engine
  in this revision). Future callers select font by script rather than
  hard-coding IDs.
- FE_MAX_FONTS bumped from 4 to 6 to leave room for a Latin UI font,
  a mono 1-bit font, an Arabic font, an Urdu font, and two spare slots.
- RTL rendering: a separate function font_engine_draw_text_scaled_rtl(),
  not a direction parameter on the LTR path.
- Shaping: offline pre-shaping for known strings (prayer names, Hijri
  months, Urdu day names) until dynamic user-entered Arabic becomes a
  requirement. No on-device HarfBuzz in the near term.

## Bottom line

Dual-format font pipeline: 1-bit panels keep MONO_1BPP unchanged;
colour panels get RGB565 with a row-blit that scales from 150 px on a
4" panel to ~250 px on 480x800 without code changes. HAL_GFX_BlitScaled
is one new primitive; a second glyph format in font_engine is one new
concept; FONT_UI_SANS lives in the font asset component; display_manager
gains one command that overlays the existing text union member without
growing it; ui_producer becomes a pure consumer with layout math only.

Time-only on black is the PR 8 deliverable. Everything else in the
reference images is Phase 2+, tracked in ROADMAP.md.
