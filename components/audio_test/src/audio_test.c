#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "audio_test.h"
#include "hal_audio.h"

#define TAG "AUDIO_TEST"

/* Must match the sample rate hal_audio_max98357a.c configures the I2S
   channel for (I2S_STD_CLK_DEFAULT_CONFIG(44100) in HAL_Audio_Init). If
   that ever changes, update this too — there's no runtime query for it. */
#define AUDIO_TEST_SAMPLE_RATE 44100
#define AUDIO_TEST_CHUNK_SAMPLES 1024
#define AUDIO_TEST_AMPLITUDE 8000 /* ~25% of int16 full scale; leaves headroom */
#define AUDIO_TEST_FADE_MS 5      /* short fade in/out to avoid click/pop */

static volatile bool s_is_playing = false;
static volatile bool s_stop_requested = false;

static esp_err_t hal_to_esp(hal_status_t hs)
{
    return (hs == HAL_OK) ? ESP_OK : ESP_FAIL;
}

/* Fill `out` with `count` samples of a sine tone at `freq_hz`, applying a
   short linear fade in/out so starting/stopping doesn't produce an
   audible click. `phase_start` lets callers chain buffers (e.g. the
   sweep) without a phase discontinuity; returns the phase to resume at. */
static double fill_tone(int16_t *out, size_t count, double freq_hz, double phase_start)
{
    size_t fade_samples = (AUDIO_TEST_SAMPLE_RATE * AUDIO_TEST_FADE_MS) / 1000;
    if (fade_samples > count / 2) fade_samples = count / 2;

    double phase = phase_start;
    double phase_inc = 2.0 * M_PI * freq_hz / AUDIO_TEST_SAMPLE_RATE;

    for (size_t i = 0; i < count; i++) {
        double amp = AUDIO_TEST_AMPLITUDE;
        if (fade_samples > 0) {
            if (i < fade_samples) {
                amp *= (double)i / (double)fade_samples;
            } else if (i >= count - fade_samples) {
                amp *= (double)(count - 1 - i) / (double)fade_samples;
            }
        }
        out[i] = (int16_t)(amp * sin(phase));
        phase += phase_inc;
        if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
    }
    return phase;
}

/* Fill `out` with `count` samples of a linear chirp from f0 to f1 Hz
   across the whole buffer. */
static void fill_sweep(int16_t *out, size_t count, double f0, double f1)
{
    size_t fade_samples = (AUDIO_TEST_SAMPLE_RATE * AUDIO_TEST_FADE_MS) / 1000;
    if (fade_samples > count / 2) fade_samples = count / 2;

    double duration_s = (double)count / AUDIO_TEST_SAMPLE_RATE;
    double k = (f1 - f0) / duration_s; /* Hz per second */

    for (size_t i = 0; i < count; i++) {
        double t = (double)i / AUDIO_TEST_SAMPLE_RATE;
        /* Instantaneous phase for a linear frequency sweep:
           phase(t) = 2*pi*(f0*t + k*t^2/2) */
        double phase = 2.0 * M_PI * (f0 * t + 0.5 * k * t * t);

        double amp = AUDIO_TEST_AMPLITUDE;
        if (fade_samples > 0) {
            if (i < fade_samples) {
                amp *= (double)i / (double)fade_samples;
            } else if (i >= count - fade_samples) {
                amp *= (double)(count - 1 - i) / (double)fade_samples;
            }
        }
        out[i] = (int16_t)(amp * sin(phase));
    }
}

/* Push `buf` (count samples) to the HAL in chunks, re-checking
   s_stop_requested between chunks so audio_test_stop() actually cuts
   playback short instead of just being a no-op flag nobody reads. */
static esp_err_t play_buffer_chunked(const int16_t *buf, size_t count)
{
    size_t offset = 0;
    while (offset < count) {
        if (s_stop_requested) {
            ESP_LOGI(TAG, "   [STOP] Playback cut short by audio_test_stop()");
            break;
        }
        size_t chunk = count - offset;
        if (chunk > AUDIO_TEST_CHUNK_SAMPLES) chunk = AUDIO_TEST_CHUNK_SAMPLES;

        hal_status_t hs = HAL_Audio_Play(buf + offset, chunk);
        if (hs != HAL_OK) {
            ESP_LOGE(TAG, "HAL_Audio_Play failed (%d) at offset %u", (int)hs, (unsigned)offset);
            return hal_to_esp(hs);
        }
        offset += chunk;
    }
    return ESP_OK;
}

esp_err_t audio_test_init(void) {
    ESP_LOGI(TAG, "========================================");
#if CONFIG_AUDIO_TEST_SIMULATION
    ESP_LOGI(TAG, "AUDIO TEST - SIMULATION MODE");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "GPIO1 (SD_MODE) would be: INPUT_PULLDOWN");
    ESP_LOGI(TAG, "  -> SD_MODE = LOW (selects RIGHT channel)");
    ESP_LOGI(TAG, "  -> External R15 (10K) pulls to GND");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "I2S Pins (would be configured):");
    ESP_LOGI(TAG, "  -> BCLK  = GPIO%d", AUDIO_BCLK_PIN);
    ESP_LOGI(TAG, "  -> LRCK  = GPIO%d", AUDIO_LRCK_PIN);
    ESP_LOGI(TAG, "  -> DOUT  = GPIO%d", AUDIO_DOUT_PIN);
    ESP_LOGI(TAG, "  -> SD_MODE = GPIO%d", AUDIO_SD_MODE_PIN);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "MAX98357A Configuration:");
    ESP_LOGI(TAG, "  -> Channel: RIGHT (mono output)");
    ESP_LOGI(TAG, "  -> Gain: 15dB (via GAIN_SLOT)");
    ESP_LOGI(TAG, "  -> Speaker: 3-5W, 4 ohm");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Audio test initialized (SIMULATION — no hardware touched)");
    s_is_playing = false;
    s_stop_requested = false;
    return ESP_OK;
#else
    ESP_LOGI(TAG, "AUDIO TEST - HARDWARE MODE");
    ESP_LOGI(TAG, "========================================");
    hal_status_t hs = HAL_Audio_Init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "HAL_Audio_Init failed (%d)", (int)hs);
        return hal_to_esp(hs);
    }
    s_is_playing = false;
    s_stop_requested = false;
    ESP_LOGI(TAG, "Audio test initialized (real HAL/I2S path up)");
    return ESP_OK;
#endif
}

esp_err_t audio_test_play_tone(uint32_t freq, uint32_t duration_ms) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "PLAYING TEST TONE");
    ESP_LOGI(TAG, "   Frequency: %u Hz", (unsigned)freq);
    ESP_LOGI(TAG, "   Duration:  %u ms", (unsigned)duration_ms);
    ESP_LOGI(TAG, "   Channel:   RIGHT (mono)");

#if CONFIG_AUDIO_TEST_SIMULATION
    ESP_LOGI(TAG, "   [SIMULATION] Tone playing...");
    s_is_playing = true;
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    s_is_playing = false;
    ESP_LOGI(TAG, "   [SIMULATION] Tone complete!");
    return ESP_OK;
#else
    size_t count = ((uint64_t)AUDIO_TEST_SAMPLE_RATE * duration_ms) / 1000;
    int16_t *buf = malloc(count * sizeof(int16_t));
    if (!buf) {
        ESP_LOGE(TAG, "malloc(%u samples) failed", (unsigned)count);
        return ESP_ERR_NO_MEM;
    }
    fill_tone(buf, count, (double)freq, 0.0);

    s_stop_requested = false;
    s_is_playing = true;
    esp_err_t err = play_buffer_chunked(buf, count);
    s_is_playing = false;
    free(buf);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "   Tone complete");
    }
    return err;
#endif
}

esp_err_t audio_test_play_beep_sequence(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "PLAYING BEEP SEQUENCE");
    ESP_LOGI(TAG, "   Channel: RIGHT (mono)");

    int beep_durations[] = {200, 200, 200};
    int pause_durations[] = {300, 300, 0};

    s_stop_requested = false;
    for (int i = 0; i < 3; i++) {
        if (s_stop_requested) {
            ESP_LOGI(TAG, "   [STOP] Beep sequence cut short");
            break;
        }
        ESP_LOGI(TAG, "   Beep %d/3 (880Hz, %dms)", i + 1, beep_durations[i]);
        esp_err_t err = audio_test_play_tone(880, beep_durations[i]);
        if (err != ESP_OK) return err;

        if (pause_durations[i] > 0 && !s_stop_requested) {
            vTaskDelay(pdMS_TO_TICKS(pause_durations[i]));
        }
    }

    ESP_LOGI(TAG, "   Beep sequence complete");
    return ESP_OK;
}

esp_err_t audio_test_play_sweep(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "PLAYING FREQUENCY SWEEP");
    ESP_LOGI(TAG, "   Range: 200Hz -> 2000Hz");
    ESP_LOGI(TAG, "   Duration: 3000ms");
    ESP_LOGI(TAG, "   Channel: RIGHT (mono)");

#if CONFIG_AUDIO_TEST_SIMULATION
    ESP_LOGI(TAG, "   [SIMULATION] Sweep playing...");
    s_is_playing = true;
    vTaskDelay(pdMS_TO_TICKS(3000));
    s_is_playing = false;
    ESP_LOGI(TAG, "   [SIMULATION] Sweep complete!");
    return ESP_OK;
#else
    size_t count = ((uint64_t)AUDIO_TEST_SAMPLE_RATE * 3000) / 1000;
    int16_t *buf = malloc(count * sizeof(int16_t));
    if (!buf) {
        ESP_LOGE(TAG, "malloc(%u samples) failed", (unsigned)count);
        return ESP_ERR_NO_MEM;
    }
    fill_sweep(buf, count, 200.0, 2000.0);

    s_stop_requested = false;
    s_is_playing = true;
    esp_err_t err = play_buffer_chunked(buf, count);
    s_is_playing = false;
    free(buf);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "   Sweep complete");
    }
    return err;
#endif
}

esp_err_t audio_test_stop(void) {
    /* Chunked playback loops (play_buffer_chunked) poll this between
       chunks, so this actually interrupts a tone/sweep in progress
       instead of just flipping a flag nobody reads. */
    s_stop_requested = true;
    ESP_LOGI(TAG, "Audio stop requested");
    return ESP_OK;
}

bool audio_test_is_playing(void) {
    return s_is_playing;
}