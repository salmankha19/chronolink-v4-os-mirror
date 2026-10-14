/*
 * ui_producer.c
 *
 * ChronoLink V4 OS -- live clock face (HH:MM:SS)
 *
 * Application band (layer 6, Constitution section 11): this file owns UI and
 * display logic, uses the svc_time service, and never calls the HAL directly.
 *
 * Design: poll, do not subscribe. The face is a consumer of the clock, so it
 * calls svc_time_get_snapshot() from its own task -- svc_time.h licenses the
 * getters from any task -- and renders only when the formatted local
 * HH:MM:SS changes. See docs/pr6-preflight.md sections 1 and 3 for the full
 * reasoning.
 *
 *   - 250 ms poll. Bounds the display lag to <250 ms and removes the 1 Hz
 *     beat between two independent second boundaries (the same second shown
 *     twice, or skipped entirely) that a plain 1 Hz poll would produce. Four
 *     cheap getter calls per second cost nothing.
 *   - Render on change. An unchanged string submits nothing, so roughly one
 *     frame per second reaches the 3-slot queue and it cannot fill.
 *   - Clear the screen on the first frame only. A full-screen DM_CMD_CLEAR
 *     costs ~15-20 ms of DMA and lazily allocates the driver's 8 kB fill
 *     buffer, so it happens once; every later frame erases just the 64x8 text
 *     bounding box (font_engine plots lit pixels only, so without the erase
 *     the previous digits bleed through).
 *
 * The submit runs in this task, never in an esp_timer callback or the kernel
 * dispatcher: display_manager_submit_frame() can block for ~10 ms worst case
 * (5 ms mutex + 5 ms queue). The task is pinned to core 0, leaving core 1 to
 * the 60 Hz display pump in core1_task.
 */
#include "ui_producer.h"
#include "display_manager.h"
#include "svc_time.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_producer";

/* Layout: portrait 320x480, built-in 8x8 font (id 0). 8 glyphs = 64 px wide. */
#define UI_CLOCK_X 128   /* (320 - 8*8) / 2 */
#define UI_CLOCK_Y 232

#define UI_POLL_PERIOD_MS 250

/* hal_gfx_color_t is RGB565, so pack the channels instead of passing a 24-bit
   literal (0xFFFFFF would be truncated to 0xFFFF). */
#define UI_COLOR_BLACK HAL_GFX_ColorFromRGB(0x00, 0x00, 0x00)
#define UI_COLOR_WHITE HAL_GFX_ColorFromRGB(0xFF, 0xFF, 0xFF)

static TaskHandle_t s_producer_task = NULL;
static char s_last_text[16] = {0};

/* --------------------------------------------------------------------------
 * Producer task (core 0)
 * -------------------------------------------------------------------------- */

static void ui_producer_task(void *arg)
{
    (void)arg;

    bool first_frame = true;
    bool was_valid = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(UI_POLL_PERIOD_MS));

        svc_time_snapshot_t snap;
        if (svc_time_get_snapshot(&snap) != ESP_OK || !snap.valid) {
            /* Tolerated, not fatal: svc_time_start() runs after
               ui_producer_start(), and the clock is unset until an RTC read
               or an NTP sync lands. Log the transition, not every poll. */
            if (was_valid) {
                ESP_LOGW(TAG, "time not yet valid");
                was_valid = false;
            }
            continue;
        }
        if (!was_valid) {
            ESP_LOGI(TAG, "time valid, producing");
            was_valid = true;
        }

        /* local already carries the POSIX TZ / DST rule applied by svc_time. */
        char buf[16];
        snprintf(buf, sizeof buf, "%02u:%02u:%02u",
                 (unsigned)snap.local.hours,
                 (unsigned)snap.local.minutes,
                 (unsigned)snap.local.seconds);

        if (strcmp(buf, s_last_text) == 0) {
            continue;
        }

        dm_frame_t frame;
        memset(&frame, 0, sizeof frame);
        frame.flags = DM_FRAME_FLAG_NONE;

        uint8_t n = 0;

        if (first_frame) {
            /* Erase whatever the panel held before boot; once only. */
            dm_cmd_clear(&frame.cmds[n++], UI_COLOR_BLACK);
            first_frame = false;
        }

        /* The 64x8 bounding box of 8 glyphs: font_engine_draw_text() plots
           lit pixels only, so the old digits have to be erased first. */
        dm_cmd_fill_rect(&frame.cmds[n++], UI_CLOCK_X, UI_CLOCK_Y, 64, 8,
                         UI_COLOR_BLACK);
        dm_cmd_draw_text(&frame.cmds[n++], UI_CLOCK_X, UI_CLOCK_Y,
                         UI_COLOR_WHITE, /* font_id */ 0, buf);
        frame.cmd_count = n;

        /* Checked, never assumed: submit_frame() returns false silently when
           the manager was never initialised, and logs when the queue is
           full. */
        if (!display_manager_submit_frame(&frame)) {
            ESP_LOGW(TAG, "frame submit failed");
        } else {
            /* Only a submitted frame updates the memory, so a dropped frame
               is retried on the next poll. s_last_text[15] stays NUL. */
            strncpy(s_last_text, buf, sizeof s_last_text - 1);
        }
    }
}

void ui_producer_start(void)
{
    if (s_producer_task) {
        return;   /* idempotent */
    }

    /* display_manager_init() is deliberately NOT called here: core_os_init()
       already initialised it, and the second call only logs "Already
       initialized". */
    if (xTaskCreatePinnedToCore(ui_producer_task, "ui_prod", 3072, NULL,
                                tskIDLE_PRIORITY + 2, &s_producer_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "failed to create producer task");
        s_producer_task = NULL;
        return;
    }

    ESP_LOGI(TAG, "UI producer started (poll %d ms)", UI_POLL_PERIOD_MS);
}
