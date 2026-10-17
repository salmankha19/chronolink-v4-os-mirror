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
 *   6. svc_network_start()  — WiFi station; publishes EVENT_NETWORK_CONNECTED
 *   7. audio_test_init()    — Initialize audio (if enabled)
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#include "core_os.h"
#include "esp_log.h"
#include "boot_manager.h"
#include "ui_producer.h"
#include "font_engine.h"
#include "font_builtin_8x8.h"
#include "svc_time.h"
#include "svc_network.h"
#include "svc_config.h"
#include "svc_prayer_engine.h"

#ifdef CONFIG_AUDIO_TEST_ENABLE
#include "audio_test.h"
#endif

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ChronoLink v4 OS - ESP32-S3 Starting");

    if (boot_manager_init(0) != 0) {
        ESP_LOGE(TAG, "Boot manager failed, entering safe loop");
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

    /* TEMPORARY verification -- remove before commit */
    {
        typedef struct {
            const char         *name;
            int                 year, month, day;
            double              lat, lon, tz_offset;
            svc_prayer_method_t method;
            svc_prayer_madhab_t madhab;
            svc_prayer_highlat_t highlat;
        } pt_verify_row_t;

        static const pt_verify_row_t rows[] = {
            /* London in June: lat 51.5 (<55). AUTO -> MIDDLE_NIGHT. */
            { "London Jun AUTO",  2026,  6, 21,  51.5074,  -0.1278,  1.0, SVC_PRAYER_METHOD_MWL,         SVC_PRAYER_MADHAB_STANDARD, SVC_PRAYER_HIGHLAT_AUTO },
            /* London in June: explicit ANGLE_BASED. */
            { "London Jun ANGLE", 2026,  6, 21,  51.5074,  -0.1278,  1.0, SVC_PRAYER_METHOD_MWL,         SVC_PRAYER_MADHAB_STANDARD, SVC_PRAYER_HIGHLAT_ANGLE_BASED },
            /* Reykjavik in June: lat 64.1 (>55). AUTO -> SEVENTH. */
            { "Reykjavik Jun AUTO", 2026, 6, 21, 64.1466, -21.9426,  0.0, SVC_PRAYER_METHOD_MWL,         SVC_PRAYER_MADHAB_STANDARD, SVC_PRAYER_HIGHLAT_AUTO },
            /* Karachi Hanafi: Standard Asr shadow factor 2. */
            { "Karachi Hanafi",   2026,  3, 20,  24.8607,  67.0011,  5.0, SVC_PRAYER_METHOD_KARACHI,     SVC_PRAYER_MADHAB_HANAFI,   SVC_PRAYER_HIGHLAT_AUTO },
            /* New York: Standard winter path, no fallback. */
            { "New York Dec",     2026, 12, 21,  40.7128, -74.0060, -5.0, SVC_PRAYER_METHOD_ISNA,        SVC_PRAYER_MADHAB_STANDARD, SVC_PRAYER_HIGHLAT_AUTO },
        };

        for (unsigned i = 0; i < (unsigned)(sizeof(rows) / sizeof(rows[0])); i++) {
            const pt_verify_row_t *r = &rows[i];
            svc_prayer_times_t pt;
            esp_err_t err = svc_prayer_engine_calculate(
                r->year, r->month, r->day, r->lat, r->lon, r->tz_offset,
                r->method, r->madhab, r->highlat, &pt);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "[PT-VERIFY] %s: calculate failed (%s)", r->name, esp_err_to_name(err));
                continue;
            }

            ESP_LOGI(TAG, "[PT-VERIFY] %s: Fajr %02u:%02u | Sunrise %02u:%02u | "
                          "Dhuhr %02u:%02u | Asr %02u:%02u | Maghrib %02u:%02u | Isha %02u:%02u",
                     r->name,
                     (unsigned)(pt.fajr_sec / 3600U),    (unsigned)((pt.fajr_sec % 3600U) / 60U),
                     (unsigned)(pt.sunrise_sec / 3600U), (unsigned)((pt.sunrise_sec % 3600U) / 60U),
                     (unsigned)(pt.dhuhr_sec / 3600U),   (unsigned)((pt.dhuhr_sec % 3600U) / 60U),
                     (unsigned)(pt.asr_sec / 3600U),     (unsigned)((pt.asr_sec % 3600U) / 60U),
                     (unsigned)(pt.maghrib_sec / 3600U), (unsigned)((pt.maghrib_sec % 3600U) / 60U),
                     (unsigned)(pt.isha_sec / 3600U),    (unsigned)((pt.isha_sec % 3600U) / 60U));
        }
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

    /* svc_network is non-fatal: no AP reachable still leaves the clock
       running off the RTC. Errors surface via EVENT_NETWORK_ERROR. */
    esp_err_t net_err = svc_network_start();
    
    if (net_err != ESP_OK) {
        ESP_LOGW(TAG, "svc_network_start failed: %s -- continuing without WiFi",
                 esp_err_to_name(net_err));
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

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}