#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hal_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Display Manager — ChronoLink V4 OS
 *
 * Declarative frame executor. Producers (Core 0 / services) build lightweight
 * dm_frame_t descriptors and submit them to a lock-free queue. The consumer
 * (Core 1 / core1_task) calls display_manager_process_frame() at ~60 Hz to
 * dequeue and execute commands via HAL_GFX.
 *
 * Design constraints:
 *   - Zero heap allocation after init.
 *   - Frame struct is small enough to be copied by value into the FreeRTOS queue.
 *   - Icon registry holds const flash pointers for zero-copy blitting.
 * -------------------------------------------------------------------------- */

#define DM_MAX_CMDS_PER_FRAME 16
#define DM_MAX_TEXT_LEN       8
#define DM_MAX_ICONS          16
#define DM_FRAME_QUEUE_DEPTH  3   /* triple-buffer + 1 spare */

/* Frame flags */
#define DM_FRAME_FLAG_NONE      0x00
#define DM_FRAME_FLAG_DISABLE_SWAP 0x01
#define DM_FRAME_FLAG_FORCE_SWAP   0x02

typedef enum {
    DM_CMD_NOP = 0,
    DM_CMD_CLEAR,            /* arg.clear.color  (fills entire screen) */
    DM_CMD_FILL_RECT,        /* arg.rect         */
    DM_CMD_DRAW_RECT,        /* arg.rect         */
    DM_CMD_DRAW_LINE,        /* arg.line         */
    DM_CMD_DRAW_CIRCLE,      /* arg.circle       */
    DM_CMD_FILL_CIRCLE,      /* arg.circle       */
    DM_CMD_DRAW_ROUND_RECT,  /* arg.round_rect   */
    DM_CMD_FILL_ROUND_RECT,  /* arg.round_rect   */
    DM_CMD_DRAW_TEXT,        /* arg.text         */
    DM_CMD_BLIT_ICON,        /* arg.icon         */
    DM_CMD_SET_CLIP,         /* arg.clip         */
    DM_CMD_RESET_CLIP,       /* no args          */
    DM_CMD_SET_SCROLL_AREA,  /* arg.scroll_area  */
    DM_CMD_SET_SCROLL_START, /* arg.scroll_start */
    DM_CMD_SWAP,             /* no args (present)*/
} dm_cmd_type_t;

typedef struct {
    dm_cmd_type_t type;
    union {
        struct { hal_gfx_color_t color; } clear;
        struct { int16_t x, y, w, h; hal_gfx_color_t color; } rect;
        struct { int16_t x0, y0, x1, y1; hal_gfx_color_t color; } line;
        struct { int16_t cx, cy, r; hal_gfx_color_t color; } circle;
        struct { int16_t x, y, w, h, r; hal_gfx_color_t color; } round_rect;
        struct { int16_t x, y; hal_gfx_color_t color; uint8_t font_id; char text[DM_MAX_TEXT_LEN]; } text;
        struct { int16_t x, y; uint16_t icon_id; } icon;
        struct { int16_t x, y, w, h; } clip;
        struct { uint16_t tfa, vsa, bfa; } scroll_area;
        struct { uint16_t vss; } scroll_start;
    } arg;
} dm_cmd_t;

typedef struct {
    uint8_t cmd_count;                  /* Valid commands in this frame (0..DM_MAX_CMDS_PER_FRAME) */
    uint8_t flags;                      /* DM_FRAME_FLAG_* */
    dm_cmd_t cmds[DM_MAX_CMDS_PER_FRAME];
} dm_frame_t;

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

void display_manager_init(void);
void display_manager_process_frame(void);
void display_manager_clear_queue(void);

/* --------------------------------------------------------------------------
 * Producer API (safe from any task / core)
 * -------------------------------------------------------------------------- */

bool display_manager_submit_frame(const dm_frame_t *frame);

/* --------------------------------------------------------------------------
 * Icon registry (call during init, before core_os_start)
 * -------------------------------------------------------------------------- */

bool display_manager_register_icon(uint16_t id,
                                   const hal_gfx_color_t *data,
                                   int16_t w,
                                   int16_t h);

/* --------------------------------------------------------------------------
 * Inline helpers for building frames cleanly
 * -------------------------------------------------------------------------- */

static inline void dm_cmd_nop(dm_cmd_t *c) {
    c->type = DM_CMD_NOP;
}

static inline void dm_cmd_clear(dm_cmd_t *c, hal_gfx_color_t color) {
    c->type = DM_CMD_CLEAR;
    c->arg.clear.color = color;
}

static inline void dm_cmd_fill_rect(dm_cmd_t *c, int16_t x, int16_t y,
                                    int16_t w, int16_t h, hal_gfx_color_t color) {
    c->type = DM_CMD_FILL_RECT;
    c->arg.rect.x = x; c->arg.rect.y = y;
    c->arg.rect.w = w; c->arg.rect.h = h;
    c->arg.rect.color = color;
}

static inline void dm_cmd_draw_rect(dm_cmd_t *c, int16_t x, int16_t y,
                                    int16_t w, int16_t h, hal_gfx_color_t color) {
    c->type = DM_CMD_DRAW_RECT;
    c->arg.rect.x = x; c->arg.rect.y = y;
    c->arg.rect.w = w; c->arg.rect.h = h;
    c->arg.rect.color = color;
}

static inline void dm_cmd_draw_line(dm_cmd_t *c, int16_t x0, int16_t y0,
                                    int16_t x1, int16_t y1, hal_gfx_color_t color) {
    c->type = DM_CMD_DRAW_LINE;
    c->arg.line.x0 = x0; c->arg.line.y0 = y0;
    c->arg.line.x1 = x1; c->arg.line.y1 = y1;
    c->arg.line.color = color;
}

static inline void dm_cmd_draw_circle(dm_cmd_t *c, int16_t cx, int16_t cy,
                                      int16_t r, hal_gfx_color_t color) {
    c->type = DM_CMD_DRAW_CIRCLE;
    c->arg.circle.cx = cx; c->arg.circle.cy = cy;
    c->arg.circle.r  = r;
    c->arg.circle.color = color;
}

static inline void dm_cmd_fill_circle(dm_cmd_t *c, int16_t cx, int16_t cy,
                                      int16_t r, hal_gfx_color_t color) {
    c->type = DM_CMD_FILL_CIRCLE;
    c->arg.circle.cx = cx; c->arg.circle.cy = cy;
    c->arg.circle.r  = r;
    c->arg.circle.color = color;
}

static inline void dm_cmd_draw_round_rect(dm_cmd_t *c, int16_t x, int16_t y,
                                          int16_t w, int16_t h, int16_t r,
                                          hal_gfx_color_t color) {
    c->type = DM_CMD_DRAW_ROUND_RECT;
    c->arg.round_rect.x = x; c->arg.round_rect.y = y;
    c->arg.round_rect.w = w; c->arg.round_rect.h = h;
    c->arg.round_rect.r = r;
    c->arg.round_rect.color = color;
}

static inline void dm_cmd_fill_round_rect(dm_cmd_t *c, int16_t x, int16_t y,
                                          int16_t w, int16_t h, int16_t r,
                                          hal_gfx_color_t color) {
    c->type = DM_CMD_FILL_ROUND_RECT;
    c->arg.round_rect.x = x; c->arg.round_rect.y = y;
    c->arg.round_rect.w = w; c->arg.round_rect.h = h;
    c->arg.round_rect.r = r;
    c->arg.round_rect.color = color;
}

static inline void dm_cmd_draw_text(dm_cmd_t *c, int16_t x, int16_t y,
                                    hal_gfx_color_t color, uint8_t font_id,
                                    const char *text) {
    c->type = DM_CMD_DRAW_TEXT;
    c->arg.text.x = x; c->arg.text.y = y;
    c->arg.text.color = color;
    c->arg.text.font_id = font_id;
    for (int i = 0; i < DM_MAX_TEXT_LEN; i++) {
        c->arg.text.text[i] = (text && text[i]) ? text[i] : '\0';
    }
}

static inline void dm_cmd_blit_icon(dm_cmd_t *c, int16_t x, int16_t y, uint16_t icon_id) {
    c->type = DM_CMD_BLIT_ICON;
    c->arg.icon.x = x; c->arg.icon.y = y;
    c->arg.icon.icon_id = icon_id;
}

static inline void dm_cmd_set_clip(dm_cmd_t *c, int16_t x, int16_t y,
                                   int16_t w, int16_t h) {
    c->type = DM_CMD_SET_CLIP;
    c->arg.clip.x = x; c->arg.clip.y = y;
    c->arg.clip.w = w; c->arg.clip.h = h;
}

static inline void dm_cmd_reset_clip(dm_cmd_t *c) {
    c->type = DM_CMD_RESET_CLIP;
}

static inline void dm_cmd_set_scroll_area(dm_cmd_t *c, uint16_t tfa,
                                          uint16_t vsa, uint16_t bfa) {
    c->type = DM_CMD_SET_SCROLL_AREA;
    c->arg.scroll_area.tfa = tfa;
    c->arg.scroll_area.vsa = vsa;
    c->arg.scroll_area.bfa = bfa;
}

static inline void dm_cmd_set_scroll_start(dm_cmd_t *c, uint16_t vss) {
    c->type = DM_CMD_SET_SCROLL_START;
    c->arg.scroll_start.vss = vss;
}

static inline void dm_cmd_swap(dm_cmd_t *c) {
    c->type = DM_CMD_SWAP;
}

#ifdef __cplusplus
}
#endif