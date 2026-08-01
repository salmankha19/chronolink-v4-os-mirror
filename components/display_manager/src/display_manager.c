#include "display_manager.h"
#include "font_engine.h"
#include "hal_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display_manager";

/* --------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------- */

static QueueHandle_t s_frame_queue = NULL;
static SemaphoreHandle_t s_submit_mutex = NULL;
static bool s_initialized = false;

/* Icon registry — populated at init time, read-only in hot path */
static struct {
    uint16_t id;
    const hal_gfx_color_t *data;
    int16_t w;
    int16_t h;
    bool valid;
} s_icons[DM_MAX_ICONS];

/* --------------------------------------------------------------------------
 * Icon registry
 * -------------------------------------------------------------------------- */

bool display_manager_register_icon(uint16_t id,
                                   const hal_gfx_color_t *data,
                                   int16_t w,
                                   int16_t h)
{
    if (!data || w <= 0 || h <= 0) {
        ESP_LOGE(TAG, "Invalid icon params id=%d", id);
        return false;
    }

    for (int i = 0; i < DM_MAX_ICONS; i++) {
        if (!s_icons[i].valid) {
            s_icons[i].id   = id;
            s_icons[i].data = data;
            s_icons[i].w    = w;
            s_icons[i].h    = h;
            s_icons[i].valid = true;
            ESP_LOGI(TAG, "Registered icon id=%d (%dx%d) @ %p", id, w, h, (const void *)data);
            return true;
        }
    }

    ESP_LOGW(TAG, "Icon registry full (max=%d), cannot register id=%d", DM_MAX_ICONS, id);
    return false;
}

/* --------------------------------------------------------------------------
 * Command executor (Core 1 hot path — no blocking, no allocation)
 * -------------------------------------------------------------------------- */

static void dm_execute_cmd(const dm_cmd_t *cmd)
{
    if (!cmd) return;

    switch (cmd->type) {
    case DM_CMD_CLEAR:
        HAL_GFX_FillRect(0, 0,
                         HAL_GFX_GetDisplayWidth(),
                         HAL_GFX_GetDisplayHeight(),
                         cmd->arg.clear.color);
        break;

    case DM_CMD_FILL_RECT:
        HAL_GFX_FillRect(cmd->arg.rect.x, cmd->arg.rect.y,
                         cmd->arg.rect.w, cmd->arg.rect.h,
                         cmd->arg.rect.color);
        break;

    case DM_CMD_DRAW_RECT:
        HAL_GFX_DrawRect(cmd->arg.rect.x, cmd->arg.rect.y,
                         cmd->arg.rect.w, cmd->arg.rect.h,
                         cmd->arg.rect.color);
        break;

    case DM_CMD_DRAW_LINE:
        HAL_GFX_DrawLine(cmd->arg.line.x0, cmd->arg.line.y0,
                         cmd->arg.line.x1, cmd->arg.line.y1,
                         cmd->arg.line.color);
        break;

    case DM_CMD_DRAW_CIRCLE:
        HAL_GFX_DrawCircle(cmd->arg.circle.cx, cmd->arg.circle.cy,
                           cmd->arg.circle.r,
                           cmd->arg.circle.color);
        break;

    case DM_CMD_FILL_CIRCLE:
        HAL_GFX_FillCircle(cmd->arg.circle.cx, cmd->arg.circle.cy,
                           cmd->arg.circle.r,
                           cmd->arg.circle.color);
        break;

    case DM_CMD_DRAW_ROUND_RECT:
        HAL_GFX_DrawRoundRect(cmd->arg.round_rect.x, cmd->arg.round_rect.y,
                              cmd->arg.round_rect.w, cmd->arg.round_rect.h,
                              cmd->arg.round_rect.r,
                              cmd->arg.round_rect.color);
        break;

    case DM_CMD_FILL_ROUND_RECT:
        HAL_GFX_FillRoundRect(cmd->arg.round_rect.x, cmd->arg.round_rect.y,
                              cmd->arg.round_rect.w, cmd->arg.round_rect.h,
                              cmd->arg.round_rect.r,
                              cmd->arg.round_rect.color);
        break;

    case DM_CMD_SET_CLIP:
        HAL_GFX_SetClip(cmd->arg.clip.x, cmd->arg.clip.y,
                        cmd->arg.clip.w, cmd->arg.clip.h);
        break;

    case DM_CMD_RESET_CLIP:
        HAL_GFX_ResetClip();
        break;

    case DM_CMD_BLIT_ICON: {
        int idx = -1;
        for (int i = 0; i < DM_MAX_ICONS; i++) {
            if (s_icons[i].valid && s_icons[i].id == cmd->arg.icon.icon_id) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) {
            HAL_GFX_Blit(s_icons[idx].data,
                         s_icons[idx].w, s_icons[idx].h,
                         cmd->arg.icon.x, cmd->arg.icon.y,
                         0, 0,
                         s_icons[idx].w, s_icons[idx].h);
        } else {
            ESP_LOGW(TAG, "Icon id=%d not found", cmd->arg.icon.icon_id);
        }
        break;
    }

    case DM_CMD_SET_SCROLL_AREA:
        HAL_Display_SetScrollArea(cmd->arg.scroll_area.tfa,
                                  cmd->arg.scroll_area.vsa,
                                  cmd->arg.scroll_area.bfa);
        break;

    case DM_CMD_SET_SCROLL_START:
        HAL_Display_SetScrollStart(cmd->arg.scroll_start.vss);
        break;

    case DM_CMD_SWAP:
        HAL_Display_Show();
        break;

    case DM_CMD_DRAW_TEXT:
        font_engine_draw_text(cmd->arg.text.x, cmd->arg.text.y,
                              cmd->arg.text.color,
                              cmd->arg.text.font_id,
                              cmd->arg.text.text);
        break;

    case DM_CMD_NOP:
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void display_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    /* GFX layer needs to query backend runtime resolution */
    HAL_GFX_Init();

    s_frame_queue = xQueueCreate(DM_FRAME_QUEUE_DEPTH, sizeof(dm_frame_t));
    if (!s_frame_queue) {
        ESP_LOGE(TAG, "Failed to create frame queue (depth=%d)", DM_FRAME_QUEUE_DEPTH);
        return;
    }

    s_submit_mutex = xSemaphoreCreateMutex();
    if (!s_submit_mutex) {
        ESP_LOGE(TAG, "Failed to create submit mutex");
        vQueueDelete(s_frame_queue);
        s_frame_queue = NULL;
        return;
    }

    memset(s_icons, 0, sizeof(s_icons));

    s_initialized = true;
    ESP_LOGI(TAG, "Display manager ready (%dx%d, queue depth=%d, max cmds=%d)",
             HAL_GFX_GetDisplayWidth(),
             HAL_GFX_GetDisplayHeight(),
             DM_FRAME_QUEUE_DEPTH,
             DM_MAX_CMDS_PER_FRAME);
}

bool display_manager_submit_frame(const dm_frame_t *frame)
{
    if (!s_initialized || !frame || !s_submit_mutex) {
        return false;
    }

    if (frame->cmd_count == 0) {
        return true; /* Nothing to do */
    }

    bool ok = false;

    if (xSemaphoreTake(s_submit_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (xQueueSend(s_frame_queue, frame, pdMS_TO_TICKS(5)) == pdPASS) {
            ok = true;
        } else {
            ESP_LOGW(TAG, "Frame queue full, dropping frame");
        }
        xSemaphoreGive(s_submit_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to acquire submit mutex, frame dropped");
    }

    return ok;
}

void display_manager_process_frame(void)
{
    if (!s_initialized || !s_frame_queue) {
        return;
    }

    dm_frame_t frame;
    if (xQueueReceive(s_frame_queue, &frame, 0) != pdPASS) {
        return;
    }

    /* ------------------------------------------------------------------
     * Reset persistent state so frames are self-contained.
     * ------------------------------------------------------------------ */
    HAL_GFX_ResetClip();
    HAL_Display_SetScrollStart(0);

    uint8_t n = frame.cmd_count;
    if (n > DM_MAX_CMDS_PER_FRAME) {
        n = DM_MAX_CMDS_PER_FRAME;
    }

    bool swap_executed = false;
    const bool disable_swap = (frame.flags & DM_FRAME_FLAG_DISABLE_SWAP) != 0;

    for (uint8_t i = 0; i < n; i++) {
        const dm_cmd_t *cmd = &frame.cmds[i];

        if (cmd->type == DM_CMD_SWAP && disable_swap) {
            continue; /* honour DISABLE_SWAP flag */
        }

        dm_execute_cmd(cmd);

        if (cmd->type == DM_CMD_SWAP) {
            swap_executed = true;
        }
    }

    /* Force swap if requested and not already done (and not disabled) */
    if ((frame.flags & DM_FRAME_FLAG_FORCE_SWAP) && !swap_executed && !disable_swap) {
        HAL_Display_Show();
    }
}

void display_manager_clear_queue(void)
{
    if (s_frame_queue) {
        xQueueReset(s_frame_queue);
    }
}