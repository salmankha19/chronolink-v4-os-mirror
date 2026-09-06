/*
 * main.c
 *
 * ChronoLink V4 OS — Application Entry Point
 *
 * Boot sequence:
 *   1. boot_manager_init()  — NVS, FATFS, HAL, Core OS
 *   2. font_engine_register() — Register built-in 8×8 font (id = 0)
 *   3. ui_producer_start()  — Start declarative UI frame producer
 *   4. audio_test_init()    — Initialize audio (if enabled)
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#include "project_config.h"
#include "core_os.h"
#include "esp_log.h"
#include "boot_manager.h"
#include "ui_producer.h"
#include "font_engine.h"
#include "font_builtin_8x8.h"

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

    ui_producer_start();

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