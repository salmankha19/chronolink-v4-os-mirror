#pragma once

#include <stddef.h>
#include <stdint.h>

#define JSON_EXEC_MAX_LEN 512

typedef enum {
    JSON_CMD_NONE = 0,
    JSON_CMD_CLEAR,
    JSON_CMD_FILL_RECT,
    JSON_CMD_DRAW_RECT,
    JSON_CMD_DRAW_LINE,
    JSON_CMD_DRAW_CIRCLE,
    JSON_CMD_FILL_CIRCLE,
    JSON_CMD_DRAW_PIXEL,
    JSON_CMD_SET_CLIP,
    JSON_CMD_RESET_CLIP,
    JSON_CMD_DRAW_TEXT,
    JSON_CMD_BLIT,
    JSON_CMD_SHOW,
} json_cmd_type_t;

/*
 * Parse and execute one flat JSON display command.
 * - No heap allocation inside (stack only).
 * - Unknown commands: log warning, return -1 (non-fatal).
 * - Missing numeric fields default to 0.
 * - Missing color defaults to "#000000".
 * - Clamp all coordinates to HAL_GFX_GetDisplayWidth/Height.
 */
int json_execute(const char *json, size_t len, char *err_buf, size_t err_buf_sz);
