/*
 * main.c
 *
 * ChronoLink V4 OS — Application Entry Point
 *
 * Boot sequence:
 *   1. boot_manager_init()  — NVS, FATFS, HAL, Core OS
 *   2. font_engine_register() — Register built-in 8×8 font (id = 0)
 *   3. svc_config_start()   — Configuration service (NVS namespace "config")
 *   4. ui_producer_start()  — Start declarative UI frame producer
 *   5. svc_time_start()     — RTC + NTP time service (first event-bus subscriber)
 *   6. svc_timetable_init() — Load year timetable from /storage (if present)
 *   7. svc_prayer_engine_service_start() — Prayer times + EVENT_PRAYER_TIMES_UPDATED
 *   8. svc_network_start()  — WiFi station; publishes EVENT_NETWORK_CONNECTED
 *   9. svc_http_start()     — HTTP server (REST + web UI + OTA)
 *  10. svc_scheduler_init/start() — 1 Hz prayer poll, EVENT_PRAYER_TIME_REACHED
 *  11. audio_test_init()    — Initialize audio (if enabled)
 *  12. esp_ota_mark_app_valid_cancel_rollback() — mark new OTA image valid
 *      only after all services above are running (#129 R2: was previously
 *      called inside boot_manager_init(), before any service started).
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#include "core_os.h"
#include "esp_log.h"
#include "esp_ota_ops.h"       /* #129 (R2): rollback mark-valid/invalid */
#include "boot_manager.h"
#include "ui_producer.h"
#include "font_engine.h"
#include "font_builtin_8x8.h"
#include "svc_time.h"
#include "svc_network.h"
#include "svc_config.h"
#include "svc_prayer_engine_service.h"
#include "svc_timetable.h"
#include "svc_scheduler.h"
#include "svc_http.h"

#ifdef CONFIG_AUDIO_TEST_ENABLE
#include "audio_test.h"
#endif

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ChronoLink v4 OS - ESP32-S3 Starting");

    if (boot_manager_init(0) != 0) {
        /* #129 (R2): if boot_manager_init() failed, the running image is
           not proven healthy. If rollback is available (a new OTA image
           not yet marked valid), mark it invalid and reboot into the
           previous slot. If not (first flash, or rollback disabled), the
           call returns and we fall through to a safe spin loop. */
        ESP_LOGE(TAG, "Boot manager failed -- attempting OTA rollback");
        esp_err_t rb_err = esp_ota_mark_app_invalid_rollback_and_reboot();
        ESP_LOGE(TAG,
                 "esp_ota_mark_app_invalid_rollback_and_reboot returned %s -- entering safe loop",
                 esp_err_to_name(rb_err));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* Register built-in font before any UI producer uses it */
    font_engine_register(&FONT_BUILTIN_8X8);

    /* svc_config is a data-only service: no background task, no events.
       Start it before anything that needs location/calculation inputs.
       Non-fatal: on failure, defaults are used and setters return
       ESP_ERR_INVALID_STATE. */
    esp_err_t config_err = svc_config_start();
    if (config_err != ESP_OK) {
        ESP_LOGW(TAG, "svc_config_start failed: %s -- running on defaults",
                 esp_err_to_name(config_err));
    }

    ui_producer_start();

    /* svc_time subscribes to EVENT_NETWORK_CONNECTED, so it has to be up
       before svc_network can publish one. Non-fatal: without a network (or
       without an RTC) the clock still runs, it just is not NTP-synced. */
    esp_err_t time_err = svc_time_start();
    if (time_err != ESP_OK) {
        ESP_LOGW(TAG, "svc_time_start failed: %s -- continuing without NTP",
                 esp_err_to_name(time_err));
    }

    /* svc_prayer_engine_service reads svc_config and svc_time, so it
       must start AFTER svc_time_start(). Its initial calculation will
       succeed if svc_time already has a valid time (RTC present).
       Otherwise it starts in "not valid" state and warms up on the
       first EVENT_TIME_UPDATE (successful NTP sync).
       Non-fatal on failure. */
    /* Initialise timetable BEFORE the prayer service starts, so the first
       recalculation can use the file if present. */
    {
        esp_err_t tt_err = svc_timetable_init();
        if (tt_err != ESP_OK) {
            ESP_LOGI(TAG, "svc_timetable_init: %s (will regenerate)",
                     esp_err_to_name(tt_err));
        }
    }

    esp_err_t prayer_err = svc_prayer_engine_service_start();
    if (prayer_err != ESP_OK) {
        ESP_LOGW(TAG, "svc_prayer_engine_service_start failed: %s",
                 esp_err_to_name(prayer_err));
    }

    /* svc_network is non-fatal: no AP reachable still leaves the clock
       running off the RTC. Errors surface via EVENT_NETWORK_ERROR. */
    esp_err_t net_err = svc_network_start();
    
    if (net_err != ESP_OK) {
        ESP_LOGW(TAG, "svc_network_start failed: %s -- continuing without WiFi",
                 esp_err_to_name(net_err));
    }

    /* Start HTTP server (PR 4.1 - Backend APIs only) */
    esp_err_t http_err = svc_http_start();
    if (http_err != ESP_OK) {
        ESP_LOGW(TAG, "svc_http_start failed: %s", esp_err_to_name(http_err));
    }

    /* Prayer scheduler (PR 5.6). Runs after the prayer service so the first
       get_times() has a chance to succeed; it is non-fatal either way --
       until location and a trusted time exist the state stays empty and the
       1 Hz poll fills it in later. init() marks the prayers that already
       passed today, so a mid-day boot does not replay them. */
    esp_err_t sched_err = svc_scheduler_init();
    if (sched_err != ESP_OK) {
        ESP_LOGW(TAG, "svc_scheduler_init failed: %s", esp_err_to_name(sched_err));
    }
    sched_err = svc_scheduler_start();
    if (sched_err != ESP_OK) {
        ESP_LOGW(TAG, "svc_scheduler_start failed: %s", esp_err_to_name(sched_err));
    }

    ESP_LOGI(TAG, "ChronoLink initialized successfully");

#ifdef CONFIG_AUDIO_TEST_ENABLE
    /* Audio test - runs after system is fully initialized */
    ESP_LOGI(TAG, "Audio test enabled - initializing...");
    
    esp_err_t err = audio_test_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Audio test initialized successfully ✅");
        
        /* Play test tone (440Hz for 1 second) */
        err = audio_test_play_tone(440, 1000);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Test tone played successfully ✅");
        } else {
            ESP_LOGE(TAG, "Failed to play test tone: %s", esp_err_to_name(err));
        }
        
        /* Play beep sequence */
        vTaskDelay(pdMS_TO_TICKS(500));
        audio_test_play_beep_sequence();
        
    } else {
        ESP_LOGE(TAG, "Audio test initialization failed: %s", esp_err_to_name(err));
    }
#endif

    /* #129 (R2): mark the OTA image valid only after every service has
       started. Previously this ran at the end of boot_manager_init(),
       before any of the services above were up -- a bad OTA image that
       crashed in svc_config / svc_time / svc_network / svc_http /
       svc_scheduler was already marked valid, so rollback could not
       catch it. The mark-valid call was moved here from boot_manager.c.
       A "stable-run check" (delay N seconds before marking valid) is
       deliberately not added here; see PR #129 description. */
    {
        esp_err_t ota_err = esp_ota_mark_app_valid_cancel_rollback();
        if (ota_err == ESP_OK) {
            ESP_LOGI(TAG, "OTA image marked valid (rollback cancelled)");
        } else if (ota_err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
            ESP_LOGD(TAG, "OTA mark-valid skipped (already valid or not OTA'd)");
        } else {
            ESP_LOGW(TAG, "esp_ota_mark_app_valid_cancel_rollback failed: %s",
                     esp_err_to_name(ota_err));
        }
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}