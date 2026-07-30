#include "json_display_executor.h"

#include "esp_log.h"
#include "hal_display.h"
#include "hal_gfx.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "json_exec";

static void set_err(char *err_buf, size_t err_buf_sz, const char *msg)
{
    if ((err_buf == NULL) || (err_buf_sz == 0U)) {
        return;
    }

    (void)snprintf(err_buf, err_buf_sz, "%s", msg);
}

static bool is_ws(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');
}

static size_t skip_ws(const char *s, size_t len, size_t pos)
{
    while ((pos < len) && is_ws(s[pos])) {
        ++pos;
    }
    return pos;
}

static int hex_nibble(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return (int)(c - '0');
    }
    if ((c >= 'a') && (c <= 'f')) {
        return (int)(10 + (c - 'a'));
    }
    if ((c >= 'A') && (c <= 'F')) {
        return (int)(10 + (c - 'A'));
    }
    return -1;
}

static bool json_find_key_value_start(const char *json,
                                      size_t len,
                                      const char *key,
                                      size_t *value_pos)
{
    const size_t key_len = strlen(key);

    if ((json == NULL) || (key == NULL) || (value_pos == NULL) || (key_len == 0U)) {
        return false;
    }

    for (size_t i = 0; (i + key_len + 2U) <= len; ++i) {
        if (json[i] != '"') {
            continue;
        }
        if ((i + key_len + 1U >= len) || (json[i + key_len + 1U] != '"')) {
            continue;
        }
        if (memcmp(&json[i + 1U], key, key_len) != 0) {
            continue;
        }

        size_t pos = i + key_len + 2U;
        pos = skip_ws(json, len, pos);
        if ((pos >= len) || (json[pos] != ':')) {
            continue;
        }

        pos = skip_ws(json, len, pos + 1U);
        if (pos >= len) {
            return false;
        }

        *value_pos = pos;
        return true;
    }

    return false;
}

static bool json_get_string_span(const char *json,
                                 size_t len,
                                 const char *key,
                                 const char **out_ptr,
                                 size_t *out_len,
                                 bool *found)
{
    size_t pos = 0U;

    if ((out_ptr == NULL) || (out_len == NULL) || (found == NULL)) {
        return false;
    }

    *found = false;
    *out_ptr = NULL;
    *out_len = 0U;

    if (!json_find_key_value_start(json, len, key, &pos)) {
        return true;
    }

    if ((pos >= len) || (json[pos] != '"')) {
        return false;
    }

    ++pos;
    const size_t start = pos;

    while ((pos < len) && (json[pos] != '"')) {
        ++pos;
    }

    if (pos >= len) {
        return false;
    }

    *found = true;
    *out_ptr = &json[start];
    *out_len = pos - start;
    return true;
}

static bool json_get_int(const char *json,
                         size_t len,
                         const char *key,
                         int32_t *out,
                         bool *found)
{
    size_t pos = 0U;

    if ((out == NULL) || (found == NULL)) {
        return false;
    }

    *found = false;

    if (!json_find_key_value_start(json, len, key, &pos)) {
        return true;
    }

    bool neg = false;
    if ((pos < len) && (json[pos] == '-')) {
        neg = true;
        ++pos;
    }

    if ((pos >= len) || (json[pos] < '0') || (json[pos] > '9')) {
        return false;
    }

    int32_t value = 0;
    while ((pos < len) && (json[pos] >= '0') && (json[pos] <= '9')) {
        value = (int32_t)(value * 10 + (int32_t)(json[pos] - '0'));
        ++pos;
    }

    *out = neg ? -value : value;
    *found = true;
    return true;
}

static bool parse_color(const char *json, size_t len, hal_gfx_color_t *out)
{
    const char *color_ptr = NULL;
    size_t color_len = 0U;
    bool found = false;

    if (out == NULL) {
        return false;
    }

    if (!json_get_string_span(json, len, "color", &color_ptr, &color_len, &found)) {
        return false;
    }

    if (!found) {
        *out = HAL_GFX_ColorFromRGB(0U, 0U, 0U);
        return true;
    }

    if ((color_len != 4U) && (color_len != 7U)) {
        return false;
    }

    if (color_ptr[0] != '#') {
        return false;
    }

    uint8_t r = 0U;
    uint8_t g = 0U;
    uint8_t b = 0U;

    if (color_len == 4U) {
        const int rn = hex_nibble(color_ptr[1]);
        const int gn = hex_nibble(color_ptr[2]);
        const int bn = hex_nibble(color_ptr[3]);
        if ((rn < 0) || (gn < 0) || (bn < 0)) {
            return false;
        }
        r = (uint8_t)(rn * 17);
        g = (uint8_t)(gn * 17);
        b = (uint8_t)(bn * 17);
    } else {
        const int r1 = hex_nibble(color_ptr[1]);
        const int r2 = hex_nibble(color_ptr[2]);
        const int g1 = hex_nibble(color_ptr[3]);
        const int g2 = hex_nibble(color_ptr[4]);
        const int b1 = hex_nibble(color_ptr[5]);
        const int b2 = hex_nibble(color_ptr[6]);
        if ((r1 < 0) || (r2 < 0) || (g1 < 0) || (g2 < 0) || (b1 < 0) || (b2 < 0)) {
            return false;
        }
        r = (uint8_t)((r1 << 4) | r2);
        g = (uint8_t)((g1 << 4) | g2);
        b = (uint8_t)((b1 << 4) | b2);
    }

    *out = HAL_GFX_ColorFromRGB(r, g, b);
    return true;
}

static int16_t clamp_coord(int32_t v, int32_t max_dim)
{
    if (max_dim <= 0) {
        return 0;
    }
    if (v < 0) {
        return 0;
    }
    if (v >= max_dim) {
        return (int16_t)(max_dim - 1);
    }
    return (int16_t)v;
}

static int16_t clamp_size(int32_t v, int32_t max_size)
{
    int32_t value = v;
    if (value < 1) {
        value = 1;
    }
    if (max_size < 1) {
        return 1;
    }
    if (value > max_size) {
        value = max_size;
    }
    return (int16_t)value;
}

static int command_from_string(const char *cmd, size_t cmd_len, json_cmd_type_t *out)
{
    if ((cmd == NULL) || (out == NULL)) {
        return -1;
    }

    if ((cmd_len == 5U) && (memcmp(cmd, "clear", 5U) == 0)) {
        *out = JSON_CMD_CLEAR;
        return 0;
    }
    if ((cmd_len == 9U) && (memcmp(cmd, "fill_rect", 9U) == 0)) {
        *out = JSON_CMD_FILL_RECT;
        return 0;
    }
    if ((cmd_len == 9U) && (memcmp(cmd, "draw_rect", 9U) == 0)) {
        *out = JSON_CMD_DRAW_RECT;
        return 0;
    }
    if ((cmd_len == 9U) && (memcmp(cmd, "draw_line", 9U) == 0)) {
        *out = JSON_CMD_DRAW_LINE;
        return 0;
    }
    if ((cmd_len == 11U) && (memcmp(cmd, "draw_circle", 11U) == 0)) {
        *out = JSON_CMD_DRAW_CIRCLE;
        return 0;
    }
    if ((cmd_len == 11U) && (memcmp(cmd, "fill_circle", 11U) == 0)) {
        *out = JSON_CMD_FILL_CIRCLE;
        return 0;
    }
    if ((cmd_len == 10U) && (memcmp(cmd, "draw_pixel", 10U) == 0)) {
        *out = JSON_CMD_DRAW_PIXEL;
        return 0;
    }
    if ((cmd_len == 8U) && (memcmp(cmd, "set_clip", 8U) == 0)) {
        *out = JSON_CMD_SET_CLIP;
        return 0;
    }
    if ((cmd_len == 10U) && (memcmp(cmd, "reset_clip", 10U) == 0)) {
        *out = JSON_CMD_RESET_CLIP;
        return 0;
    }
    if ((cmd_len == 9U) && (memcmp(cmd, "draw_text", 9U) == 0)) {
        *out = JSON_CMD_DRAW_TEXT;
        return 0;
    }
    if ((cmd_len == 4U) && (memcmp(cmd, "blit", 4U) == 0)) {
        *out = JSON_CMD_BLIT;
        return 0;
    }
    if ((cmd_len == 4U) && (memcmp(cmd, "show", 4U) == 0)) {
        *out = JSON_CMD_SHOW;
        return 0;
    }

    *out = JSON_CMD_NONE;
    return -1;
}

int json_execute(const char *json, size_t len, char *err_buf, size_t err_buf_sz)
{
    const char *cmd_ptr = NULL;
    size_t cmd_len = 0U;
    bool found = false;
    json_cmd_type_t cmd = JSON_CMD_NONE;

    if ((json == NULL) || (len == 0U)) {
        set_err(err_buf, err_buf_sz, "empty input");
        ESP_LOGW(TAG, "Malformed JSON: empty input");
        return -1;
    }

    size_t head = skip_ws(json, len, 0U);
    size_t tail = len;
    while ((tail > head) && is_ws(json[tail - 1U])) {
        --tail;
    }
    if ((head >= tail) || (json[head] != '{') || (json[tail - 1U] != '}')) {
        set_err(err_buf, err_buf_sz, "malformed object");
        ESP_LOGW(TAG, "Malformed JSON: expected flat object");
        return -1;
    }

    if (!json_get_string_span(json, len, "cmd", &cmd_ptr, &cmd_len, &found) || !found) {
        set_err(err_buf, err_buf_sz, "missing cmd");
        ESP_LOGW(TAG, "Malformed JSON or missing cmd");
        return -1;
    }

    if (command_from_string(cmd_ptr, cmd_len, &cmd) != 0) {
        set_err(err_buf, err_buf_sz, "unknown cmd");
        ESP_LOGW(TAG, "Unknown command: %.*s", (int)cmd_len, cmd_ptr);
        return -1;
    }

    int32_t disp_w = HAL_GFX_GetDisplayWidth();
    int32_t disp_h = HAL_GFX_GetDisplayHeight();
    if (disp_w <= 0) {
        disp_w = 1;
    }
    if (disp_h <= 0) {
        disp_h = 1;
    }

    hal_gfx_color_t color = HAL_GFX_ColorFromRGB(0U, 0U, 0U);
    if (!parse_color(json, len, &color)) {
        set_err(err_buf, err_buf_sz, "invalid color");
        ESP_LOGW(TAG, "Invalid color field");
        return -1;
    }

    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 1;
    int32_t h = 1;
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t cx = 0;
    int32_t cy = 0;
    int32_t r = 1;

    bool tmp_found = false;
    if (!json_get_int(json, len, "x", &x, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "y", &y, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "w", &w, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "h", &h, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "x0", &x0, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "y0", &y0, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "x1", &x1, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "y1", &y1, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "cx", &cx, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "cy", &cy, &tmp_found)) goto malformed;
    if (!json_get_int(json, len, "r", &r, &tmp_found)) goto malformed;

    switch (cmd) {
    case JSON_CMD_CLEAR:
        HAL_GFX_FillRect(0, 0, (int16_t)disp_w, (int16_t)disp_h, color);
        return 0;

    case JSON_CMD_FILL_RECT: {
        const int16_t xx = clamp_coord(x, disp_w);
        const int16_t yy = clamp_coord(y, disp_h);
        const int16_t ww = clamp_size(w, disp_w - xx);
        const int16_t hh = clamp_size(h, disp_h - yy);
        HAL_GFX_FillRect(xx, yy, ww, hh, color);
        return 0;
    }

    case JSON_CMD_DRAW_RECT: {
        const int16_t xx = clamp_coord(x, disp_w);
        const int16_t yy = clamp_coord(y, disp_h);
        const int16_t ww = clamp_size(w, disp_w - xx);
        const int16_t hh = clamp_size(h, disp_h - yy);
        HAL_GFX_DrawRect(xx, yy, ww, hh, color);
        return 0;
    }

    case JSON_CMD_DRAW_LINE: {
        const int16_t xx0 = clamp_coord(x0, disp_w);
        const int16_t yy0 = clamp_coord(y0, disp_h);
        const int16_t xx1 = clamp_coord(x1, disp_w);
        const int16_t yy1 = clamp_coord(y1, disp_h);
        HAL_GFX_DrawLine(xx0, yy0, xx1, yy1, color);
        return 0;
    }

    case JSON_CMD_DRAW_CIRCLE: {
        const int16_t ccx = clamp_coord(cx, disp_w);
        const int16_t ccy = clamp_coord(cy, disp_h);
        const int32_t max_dim = (disp_w > disp_h) ? disp_w : disp_h;
        const int16_t rr = clamp_size(r, max_dim);
        HAL_GFX_DrawCircle(ccx, ccy, rr, color);
        return 0;
    }

    case JSON_CMD_FILL_CIRCLE: {
        const int16_t ccx = clamp_coord(cx, disp_w);
        const int16_t ccy = clamp_coord(cy, disp_h);
        const int32_t max_dim = (disp_w > disp_h) ? disp_w : disp_h;
        const int16_t rr = clamp_size(r, max_dim);
        HAL_GFX_FillCircle(ccx, ccy, rr, color);
        return 0;
    }

    case JSON_CMD_DRAW_PIXEL: {
        const int16_t xx = clamp_coord(x, disp_w);
        const int16_t yy = clamp_coord(y, disp_h);
        HAL_GFX_DrawPixel(xx, yy, color);
        return 0;
    }

    case JSON_CMD_SET_CLIP: {
        const int16_t xx = clamp_coord(x, disp_w);
        const int16_t yy = clamp_coord(y, disp_h);
        const int16_t ww = clamp_size(w, disp_w - xx);
        const int16_t hh = clamp_size(h, disp_h - yy);
        HAL_GFX_SetClip(xx, yy, ww, hh);
        return 0;
    }

    case JSON_CMD_RESET_CLIP:
        HAL_GFX_ResetClip();
        return 0;

    case JSON_CMD_DRAW_TEXT: {
        const char *text_ptr = NULL;
        size_t text_len = 0U;
        if (!json_get_string_span(json, len, "text", &text_ptr, &text_len, &found)) {
            goto malformed;
        }
        const int16_t xx = clamp_coord(x, disp_w);
        const int16_t yy = clamp_coord(y, disp_h);
        ESP_LOGI(TAG, "STUB: draw_text not yet implemented (x=%d y=%d len=%u)",
                 (int)xx, (int)yy, (unsigned)text_len);
        (void)text_ptr;
        return 0;
    }

    case JSON_CMD_BLIT: {
        const char *data_ptr = NULL;
        size_t data_len = 0U;
        if (!json_get_string_span(json, len, "data", &data_ptr, &data_len, &found)) {
            goto malformed;
        }
        const int16_t xx = clamp_coord(x, disp_w);
        const int16_t yy = clamp_coord(y, disp_h);
        const int16_t ww = clamp_size(w, disp_w - xx);
        const int16_t hh = clamp_size(h, disp_h - yy);
        ESP_LOGI(TAG, "STUB: blit not yet implemented (x=%d y=%d w=%d h=%d data_len=%u)",
                 (int)xx, (int)yy, (int)ww, (int)hh, (unsigned)data_len);
        (void)data_ptr;
        return 0;
    }

    case JSON_CMD_SHOW:
        (void)HAL_Display_Show();
        return 0;

    case JSON_CMD_NONE:
    default:
        set_err(err_buf, err_buf_sz, "unknown cmd");
        ESP_LOGW(TAG, "Unknown command type");
        return -1;
    }

malformed:
    set_err(err_buf, err_buf_sz, "malformed json");
    ESP_LOGW(TAG, "Malformed JSON command payload");
    return -1;
}
