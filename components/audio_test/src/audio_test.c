#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "audio_test.h"

#define TAG "AUDIO_TEST"

static bool is_playing = false;

esp_err_t audio_test_init(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🔊 AUDIO TEST - SIMULATION MODE");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "GPIO1 (SD_MODE) would be: INPUT_PULLDOWN");
    ESP_LOGI(TAG, "  → SD_MODE = LOW (selects RIGHT channel)");
    ESP_LOGI(TAG, "  → External R15 (10K) pulls to GND ✅");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "I2S Pins (would be configured):");
    ESP_LOGI(TAG, "  → BCLK  = GPIO47 (Pin 24)");
    ESP_LOGI(TAG, "  → LRCK  = GPIO48 (Pin 25)");
    ESP_LOGI(TAG, "  → DOUT  = GPIO21 (Pin 23)");
    ESP_LOGI(TAG, "  → SD_MODE = GPIO1 (Pin 39)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "MAX98357A Configuration:");
    ESP_LOGI(TAG, "  → Channel: RIGHT (mono output)");
    ESP_LOGI(TAG, "  → Gain: 15dB (via GAIN_SLOT)");
    ESP_LOGI(TAG, "  → Speaker: 3-5W, 4Ω");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✅ Audio test initialized (SIMULATION)");
    ESP_LOGI(TAG, "   (No hardware required - build passes)");
    
    return ESP_OK;
}

esp_err_t audio_test_play_tone(uint32_t freq, uint32_t duration_ms) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔊 PLAYING TEST TONE");
    ESP_LOGI(TAG, "   Frequency: %d Hz", freq);
    ESP_LOGI(TAG, "   Duration:  %d ms", duration_ms);
    ESP_LOGI(TAG, "   Channel:   RIGHT (mono)");
    ESP_LOGI(TAG, "   [SIMULATION] Tone playing...");
    
    // Simulate playback delay
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    ESP_LOGI(TAG, "   [SIMULATION] Tone complete! ✅");
    return ESP_OK;
}

esp_err_t audio_test_play_beep_sequence(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔊 PLAYING BEEP SEQUENCE");
    ESP_LOGI(TAG, "   Channel: RIGHT (mono)");
    
    int beep_durations[] = {200, 200, 200};
    int pause_durations[] = {300, 300, 0};
    
    for (int i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "   [SIMULATION] Beep %d/3 (880Hz, %dms)", 
                 i+1, beep_durations[i]);
        vTaskDelay(pdMS_TO_TICKS(beep_durations[i]));
        
        if (pause_durations[i] > 0) {
            vTaskDelay(pdMS_TO_TICKS(pause_durations[i]));
        }
    }
    
    ESP_LOGI(TAG, "   [SIMULATION] Beep sequence complete! ✅");
    return ESP_OK;
}

esp_err_t audio_test_play_sweep(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔊 PLAYING FREQUENCY SWEEP");
    ESP_LOGI(TAG, "   Range: 200Hz → 2000Hz");
    ESP_LOGI(TAG, "   Duration: 3000ms");
    ESP_LOGI(TAG, "   Channel: RIGHT (mono)");
    ESP_LOGI(TAG, "   [SIMULATION] Sweep playing...");
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "   [SIMULATION] Sweep complete! ✅");
    return ESP_OK;
}

esp_err_t audio_test_stop(void) {
    is_playing = false;
    ESP_LOGI(TAG, "⏹️ Audio stopped");
    return ESP_OK;
}

bool audio_test_is_playing(void) {
    return is_playing;
}