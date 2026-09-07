#ifndef AUDIO_TEST_H
#define AUDIO_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio pin definitions for MAX98357A
 * Based on Chronolink-v4-os PCB design
 */
#define AUDIO_BCLK_PIN    47  // I2S Bit Clock
#define AUDIO_LRCK_PIN    48  // I2S Word Select (LRCLK)
#define AUDIO_DOUT_PIN    21  // I2S Data Out
#define AUDIO_SD_MODE_PIN 1   // GPIO1 (pulled LOW via R15)

/**
 * @brief Initialize audio subsystem for mono playback
 *
 * When CONFIG_AUDIO_TEST_SIMULATION is set, this only prints the pin/
 * config info that would be used (no hardware touched — useful for CI
 * builds and machines with no speaker wired up).
 *
 * Otherwise this calls straight into the real chronolink_hal audio
 * driver (HAL_Audio_Init -> hal_audio_max98357a.c), so a pass here means
 * the actual I2S channel + MAX98357A path came up, not just that this
 * test component compiled.
 *
 * @return ESP_OK on success
 */
esp_err_t audio_test_init(void);

/**
 * @brief Play a test tone
 * 
 * @param freq       Frequency in Hz (e.g., 440 for A4)
 * @param duration_ms Duration in milliseconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_test_play_tone(uint32_t freq, uint32_t duration_ms);

/**
 * @brief Play a beep sequence (3 beeps)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_test_play_beep_sequence(void);

/**
 * @brief Play a frequency sweep (200Hz → 2000Hz)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_test_play_sweep(void);

/**
 * @brief Stop audio playback immediately
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_test_stop(void);

/**
 * @brief Check if audio is currently playing
 * 
 * @return true if audio is playing, false otherwise
 */
bool audio_test_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_TEST_H