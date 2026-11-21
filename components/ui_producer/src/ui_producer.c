/*
 * ui_producer.c
 *
 * ChronoLink V4 OS -- live clock face (H:MM AM/PM)
 *
 * Application band (layer 6, Constitution section 11): this file owns UI and
 * display logic, uses the svc_time service, and never calls the HAL directly.
 *
 * Design: poll, do not subscribe. The face is a consumer of the clock, so it
 * calls svc_time_get_snapshot() from its own task -- svc_time.h licenses the
 * getters from any task -- and renders only when the formatted local
 * H:MM AM/PM changes. See docs/pr6-preflight.md sections 1 and 3 for the full
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
 *     buffer, so it happens once. Each frame contains one DM_CMD_DRAW_TEXT_SCALED
 *     command (the clock string), plus an optional red banner (fill + 2 text
 *     commands) when the RTC is untrusted. No per-frame erase: each scaled
 *     glyph blit paints its full rect including background.
 *
 * The submit runs in this task, never in an esp_timer callback or the kernel
 * dispatcher: display_manager_submit_frame() can block for ~10 ms worst case
 * (5 ms mutex + 5 ms queue). The task is pinned to core 0, leaving core 1 to
 * the 60 Hz display pump in core1_task.
 *
 * The task owns two screens: the provisioning AP screen (one unified layout
 * for both entry scenarios -- no credentials stored, and credential lockout)
 * and the clock face.
 * While the AP screen is up the loop submits one self-contained layout
 * (leading DM_CMD_CLEAR) and skips the clock work entirely. s_last_text holds
 * the draw key of whatever is on the panel -- "AP_<ssid>_<passphrase>" or the
 * clock key -- so a mode change, a passphrase rotation, or a time/date change
 * always forces a re-render.
 */
#include "ui_producer.h"
#include "display_manager.h"
#include "svc_time.h"
#include "svc_network.h"
#include "svc_prayer_engine_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "font_ui_sans.h"

static const char *TAG = "ui_producer";

/* Layout: portrait 320x480, font_id 1 (FONT_UI_SANS, RGB565); font_id 0
   (FONT_BUILTIN_8X8) remains the mono path for 1-bit panels.
   8 glyphs = 64 px wide. */
#define UI_CLOCK_X 128   /* (320 - 8*8) / 2 */
#define UI_CLOCK_Y 232

#define UI_POLL_PERIOD_MS 250

/* hal_gfx_color_t is RGB565, so pack the channels instead of passing a 24-bit
   literal (0xFFFFFF would be truncated to 0xFFFF). */
#define UI_COLOR_BLACK HAL_GFX_ColorFromRGB(0x00, 0x00, 0x00)
#define UI_COLOR_WHITE HAL_GFX_ColorFromRGB(0xFF, 0xFF, 0xFF)

static TaskHandle_t s_producer_task = NULL;
static char s_last_text[128] = {0};   /* matches draw_key[128]; see 5e */


/* --------------------------------------------------------------------------
 * Producer task (core 0)
 * -------------------------------------------------------------------------- */

static void ui_producer_task(void *arg)
{
    (void)arg;

    bool first_frame = true;
    bool was_valid = false;
    TickType_t boot_tick = xTaskGetTickCount();

    /* Startup splash: immediate feedback before time is ready. */
    {
        dm_frame_t splash;
        memset(&splash, 0, sizeof splash);
        splash.flags = DM_FRAME_FLAG_NONE;
        uint8_t n = 0;
        dm_cmd_clear(&splash.cmds[n++], UI_COLOR_BLACK);
        dm_cmd_draw_text_scaled(&splash.cmds[n++], 80, 200, 1, 24, "Starting...");
        splash.cmd_count = n;
        display_manager_submit_frame(&splash);
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(UI_POLL_PERIOD_MS));

        /* CR #4: single provisioning AP screen. svc_network_is_ap_mode() covers
           both entry scenarios -- no credentials stored at boot, and credential
           lockout -- and the identity is "ChronoLink-<MAC4>" either way, so
           there is one branch, not two. */
        if (svc_network_is_ap_mode()) {
            /* 33 = worst-case 802.11 SSID (32) + NUL; 17 = the 16-char
               passphrase + NUL. Both getters fail closed on a too-small buffer
               and while the identity is not ready, so a partial identity can
               never be drawn. */
            char ssid[33] = {0};
            char pass[17] = {0};

            /* AP mode can end between the predicate above and these calls
               (recovery idle/absolute timeout, stop from a worker); both then
               return ESP_ERR_INVALID_STATE. Skip the frame and retry next
               poll -- never draw stale or empty strings, and never build a key
               from half an identity. */
            if (svc_network_get_ap_ssid(ssid, sizeof(ssid)) != ESP_OK ||
                svc_network_get_ap_passphrase(pass, sizeof(pass)) != ESP_OK) {
                continue;
            }

            char draw_key[128];
            snprintf(draw_key, sizeof(draw_key), "AP_%s_%s", ssid, pass);

            if (strcmp(draw_key, s_last_text) != 0) {
                dm_frame_t frame;
                memset(&frame, 0, sizeof(frame));
                frame.flags = DM_FRAME_FLAG_NONE;

                /* 9 commands (1 clear + 8 text) against
                   DM_MAX_CMDS_PER_FRAME 16. Longest string is the 15-char
                   SSID; DM_MAX_TEXT_LEN is 32, i.e. 31 usable chars. */
                uint8_t n = 0;
                dm_cmd_clear(&frame.cmds[n++], UI_COLOR_BLACK);
                dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, 30, 1, 24,
                                        "WiFi Provisioning Mode");
                dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, 70, 1, 18,
                                        "Connect to:");
                dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, 95, 1, 18, ssid);
                dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, 125, 1, 18,
                                        "Password:");
                dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, 150, 1, 18, pass);
                dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, 180, 1, 18,
                                        "Open Browser:");
                dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, 205, 1, 18,
                                        "192.168.4.1");
                dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, 235, 1, 16,
                                        "Auto-exits when idle");
                frame.cmd_count = n;

                if (!display_manager_submit_frame(&frame)) {
                    ESP_LOGW(TAG, "AP frame submit failed");
                } else {
                    /* Copy only after a successful submit, so a dropped frame
                       is retried on the next poll (same as the clock path). */
                    strncpy(s_last_text, draw_key, sizeof(s_last_text) - 1);
                    s_last_text[sizeof(s_last_text) - 1] = '\0';
                }
            }
            continue; /* Skip the clock logic below */
        }

        /* If we just left AP mode, force a full clock redraw. first_frame
           matters here: it is what emits the DM_CMD_CLEAR, and the AP screen
           paints outside the clock's own rects (down to y=235), so without the
           clear its lines would survive underneath the clock. The AP key is
           compared by prefix because it embeds the SSID and passphrase. */
        if (strncmp(s_last_text, "AP_", 3) == 0) {
            s_last_text[0] = '\0';
            first_frame = true;
        }

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

        bool regen = svc_prayer_engine_service_is_regenerating();
        bool untrusted = snap.rtc_untrusted;
        bool show_banner = untrusted && ((xTaskGetTickCount() - boot_tick) > pdMS_TO_TICKS(60000));

        char buf[48];
        if (untrusted) {
            snprintf(buf, sizeof buf, "--:--%s", regen ? " *upd" : "");
        } else {
            int h12 = snap.local.hours % 12;
            if (h12 == 0) h12 = 12;
            const char *mer = (snap.local.hours < 12) ? "AM" : "PM";
            const char *sync = (snap.state != SVC_TIME_STATE_NTP_SYNCED) ? " *no sync" : "";
            snprintf(buf, sizeof buf, "%d:%02d%s%s%s",
                     h12, (unsigned)snap.local.minutes, mer,
                     sync, regen ? " *upd" : "");
        }

        /* Gregorian and Hijri date strings for the two lines below the
           clock. Both empty when untrusted — the red banner covers that
           state. */
        char greg_date[24]  = {0};
        char hijri_date[32] = {0};
        if (!untrusted) {
            time_t epoch = 0;
            struct tm tm_local;
            if (svc_time_get_epoch(&epoch) == ESP_OK &&
                localtime_r(&epoch, &tm_local) != NULL) {
                strftime(greg_date, sizeof(greg_date), "%a %d %b %Y", &tm_local);
            }
            svc_prayer_engine_times_t times;
            if (svc_prayer_engine_service_get_times(&times) == ESP_OK &&
                times.hijri_year != 0) {
                /* Numeric DD/MM/YYYY, all digits. Uniform 10 chars
                   regardless of month name; language-agnostic (05 is 05
                   in every script); keeps the panel's date line the
                   same width every month. Web UI keeps the word form
                   from the API's full `month_name` field. */
                snprintf(hijri_date, sizeof(hijri_date), "%02u/%02u/%u",
                         (unsigned)times.hijri_day,
                         (unsigned)times.hijri_month,
                         (unsigned)times.hijri_year);
            }
        }

        /* Unique hash including dates and banner state forces redraws on
           transitions (midnight, RTC trust change, banner show/hide).
           128 bytes covers the type-level worst case: buf[48] +
           greg_date[24] + hijri_date[32] + separators + %d + NUL = 106,
           plus headroom for future growth. */
        char draw_key[128];
        snprintf(draw_key, sizeof(draw_key), "%s|%s|%s|%d",
                 buf, greg_date, hijri_date, (int)show_banner);

        if (strcmp(draw_key, s_last_text) == 0) {
            continue;
        }

        int W = HAL_GFX_GetDisplayWidth();
        int H = HAL_GFX_GetDisplayHeight();
        /* Fit H:MM within 80% of panel width. The real (Inter SemiBold)
           font's digits are ~2:3 (w:h) and the colon ~1:3, so H:MM
           spans about 3 × target_h. Panel height bounds the size too:
           5/16 of H is the designed proportion. Take the smaller.
           The RGB565 placeholder's glyphs are square (8x8 native), so
           they overflow this box until the real font is generated;
           the pipeline still executes and clips cleanly. */
        uint16_t target_h_by_h = (uint16_t)(H * 5 / 16);
        uint16_t target_h_by_w = (uint16_t)((W * 8 / 10) / 3);
        uint16_t target_h = (target_h_by_h < target_h_by_w)
                            ? target_h_by_h : target_h_by_w;

        dm_frame_t frame;
        memset(&frame, 0, sizeof frame);
        frame.flags = DM_FRAME_FLAG_NONE;

        uint8_t n = 0;

        if (first_frame) {
            dm_cmd_clear(&frame.cmds[n++], UI_COLOR_BLACK);
            first_frame = false;
        }

        /* Center the clock string based on its actual width. */
        int16_t glyph_w = (int16_t)(target_h * 2 / 3);
        int16_t time_w  = (int16_t)(strlen(buf) * glyph_w);
        int16_t x0 = (int16_t)((W - time_w) / 2);
        int16_t y0 = (int16_t)((H - target_h) / 2);

        if (show_banner) {
            y0 -= (int16_t)(target_h / 3); /* shift time up to make room */
        }

        dm_cmd_draw_text_scaled(&frame.cmds[n++], x0, y0,
                                /* font_id */ 1, target_h, buf);

        /* Two lines below the clock: Gregorian, then Hijri. Both at h=22,
           centred. 14 px/glyph at h=22 matches the 2:3 aspect used for
           the clock. Skipped entirely when untrusted — the banner covers
           that state. */
        if (greg_date[0] || hijri_date[0]) {
            const uint16_t small_h  = 22;
            const int16_t  small_gw = 14;
            int16_t y_dates = y0 + (int16_t)target_h + 8;

            if (greg_date[0]) {
                int16_t g_w = (int16_t)(strlen(greg_date) * small_gw);
                int16_t g_x = (int16_t)((W - g_w) / 2);
                dm_cmd_draw_text_scaled(&frame.cmds[n++], g_x, y_dates,
                                        /* font_id */ 1, small_h, greg_date);
                y_dates += 26;
            }
            if (hijri_date[0]) {
                int16_t h_w = (int16_t)(strlen(hijri_date) * small_gw);
                int16_t h_x = (int16_t)((W - h_w) / 2);
                dm_cmd_draw_text_scaled(&frame.cmds[n++], h_x, y_dates,
                                        /* font_id */ 1, small_h, hijri_date);
            }
        }

        if (show_banner) {
            /* Two-line red banner at the bottom. */
            int16_t banner_y = (int16_t)(H - 70);
            dm_cmd_fill_rect(&frame.cmds[n++], 0, banner_y, (int16_t)W, 60,
                             HAL_GFX_ColorFromRGB(0xC0, 0x39, 0x2B));
            dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, (int16_t)(banner_y + 6),
                                    /* font_id */ 1, 22, "Time invalid");
            dm_cmd_draw_text_scaled(&frame.cmds[n++], 10, (int16_t)(banner_y + 32),
                                    /* font_id */ 1, 16, "Check internet");
        }

        frame.cmd_count = n;

        if (!display_manager_submit_frame(&frame)) {
            ESP_LOGW(TAG, "frame submit failed");
        } else {
            strncpy(s_last_text, draw_key, sizeof s_last_text - 1);
            s_last_text[sizeof(s_last_text) - 1] = '\0';
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

    /* Register the scalable UI font (id 1) before the task can
       submit any frame. font_engine_register() is idempotent on
       a given id; a second call after a soft restart harmlessly
       fails and is ignored. */
    if (!font_engine_register(&FONT_UI_SANS)) {
        ESP_LOGD(TAG, "scalable font register skipped (already registered or invalid)");
    }
    if (xTaskCreatePinnedToCore(ui_producer_task, "ui_prod", 3072, NULL,
                                tskIDLE_PRIORITY + 2, &s_producer_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "failed to create producer task");
        s_producer_task = NULL;
        return;
    }

    ESP_LOGI(TAG, "UI producer started (poll %d ms)", UI_POLL_PERIOD_MS);
}
