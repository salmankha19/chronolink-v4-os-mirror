/*
 * hal_audio_max98357a.c
 *
 * ChronoLink V4 OS — MAX98357A I2S Class-D Amp
 *
 * No I2C/SPI control. Just I2S TX + optional SD_MODE GPIO.
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#include "hal_audio.h"
#include "board_pins.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HAL_AUDIO";

/* Chunk size for HAL_Audio_Play(). Bounds the worst-case stop latency
   to (HAL_AUDIO_CHUNK_SAMPLES / sample_rate) ~= 23 ms at 44.1 kHz. */
#define HAL_AUDIO_CHUNK_SAMPLES     1024

/* Per-chunk write timeout. A stalled DMA must not hang the caller
   forever, which is what portMAX_DELAY did. */
#define HAL_AUDIO_WRITE_TIMEOUT_MS  1000

/* I2S and SD_MODE pins come from pdl_pins.h (included via board_pins.h
   at the top of this file). PDL_PIN_I2S_BCLK=47, PDL_PIN_I2S_WS=48,
   PDL_PIN_I2S_DOUT=21, PDL_PIN_AUDIO_SD_MODE=-1 on the shipped PCB.

   An earlier #ifndef fallback block lived here with values 26/25/27,
   routed through CONFIG_BOARD_AUDIO_*. It was unreachable: board_pins.h
   already pulls in pdl_pins.h, which unconditionally defines every
   PDL_PIN_I2S_* and PDL_PIN_AUDIO_SD_MODE macro, so the fallback
   branch never compiled in. The 26/25/27 values did not match this
   PCB anyway. Removed in PR 3.

   If software SD_MODE control is ever wanted, see the procedure
   documented in pdl_pins.h next to PDL_PIN_AUDIO_SD_MODE. */

/* SD_MODE (amp enable) is not driven by this driver on the shipped
   PCB: PDL_PIN_AUDIO_SD_MODE is -1 and the JP2 solder jumper keeps
   GPIO1 isolated from the amp, so the #if block in HAL_Audio_Init()
   below compiles out. See pdl_pins.h next to PDL_PIN_AUDIO_SD_MODE
   for the rework procedure that would enable firmware control. */
static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_inited = false;
static volatile bool s_stop_requested = false;

hal_status_t HAL_Audio_Init(void)
{
    if (s_inited) return HAL_OK;

    /* A retained handle here means a previous HAL_Audio_Deinit() failed
       to delete the channel. Calling i2s_new_channel() would overwrite
       s_tx_chan and leak the old one, so refuse and ask the caller to
       retry Deinit first (see the contract comment in Deinit). */
    if (s_tx_chan != NULL) {
        ESP_LOGE(TAG, "HAL_Audio_Init: TX channel still allocated -- "
                      "retry HAL_Audio_Deinit() first");
        return HAL_ERR_INIT;
    }

    /* Optional SD_MODE (shutdown) pin. On the shipped PCB this block
       compiles out because PDL_PIN_AUDIO_SD_MODE is -1. */
    #if defined(PDL_PIN_AUDIO_SD_MODE) && PDL_PIN_AUDIO_SD_MODE >= 0
        gpio_config_t sd_mode_conf = {
            .pin_bit_mask = (1ULL << PDL_PIN_AUDIO_SD_MODE),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t sd_err = gpio_config(&sd_mode_conf);
        if (sd_err != ESP_OK) {
            ESP_LOGW(TAG, "SD_MODE gpio_config failed: %s", esp_err_to_name(sd_err));
        } else {
            gpio_set_level(PDL_PIN_AUDIO_SD_MODE, 1);
        }
    #endif

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t ch_err = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ch_err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ch_err));
        s_tx_chan = NULL;
        return HAL_ERR_INIT;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PDL_PIN_I2S_BCLK,
            .ws   = (gpio_num_t)PDL_PIN_I2S_WS,
            .dout = (gpio_num_t)PDL_PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };

    esp_err_t init_err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (init_err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(init_err));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return HAL_ERR_INIT;
    }

    esp_err_t en_err = i2s_channel_enable(s_tx_chan);
    if (en_err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(en_err));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return HAL_ERR_INIT;
    }

    s_inited = true;
    ESP_LOGI(TAG, "MAX98357A I2S ready");
    return HAL_OK;
}

hal_status_t HAL_Audio_Deinit(void)
{
    /* Caller contract: do not call this while HAL_Audio_Play() is in
       flight on another task. There is no lock guarding s_tx_chan or
       s_inited today; a stop-then-deinit sequence should go through
       HAL_Audio_Stop() first and wait for Play to return. Enforcing
       that with a mutex is future work.

       If i2s_del_channel() failed on a previous call, retry
       HAL_Audio_Deinit() before calling HAL_Audio_Init() again:
       Init expects the TX channel slot to be free. */
    if (!s_inited && !s_tx_chan) return HAL_OK;

    hal_status_t status = HAL_OK;

    if (s_tx_chan) {
        /* Disable is best-effort: on a retry after a failed delete,
           the channel is already disabled and IDF returns
           ESP_ERR_INVALID_STATE, which is not a real failure. The
           success criterion is whether the handle is freed below. */
        esp_err_t dis_err = i2s_channel_disable(s_tx_chan);
        if (dis_err != ESP_OK && dis_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "i2s_channel_disable failed: %s", esp_err_to_name(dis_err));
            /* do not set status here; i2s_del_channel decides success */
        }

        esp_err_t del_err = i2s_del_channel(s_tx_chan);
        if (del_err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_del_channel failed: %s -- keeping handle for retry",
                     esp_err_to_name(del_err));
            status = HAL_ERR_DEV;
        } else {
            s_tx_chan = NULL;
        }
    }

    #if PDL_PIN_AUDIO_SD_MODE >= 0
        gpio_set_level(PDL_PIN_AUDIO_SD_MODE, 0);
    #endif

    s_inited = false;
    s_stop_requested = false;
    ESP_LOGI(TAG, "MAX98357A I2S deinitialized");
    return status;
}

hal_status_t HAL_Audio_Play(const int16_t *pcm, size_t samples)
{
    /* Split the arg checks so callers can tell "not initialised" apart
       from "bad argument". The old code returned HAL_ERR_INIT for both. */
    if (!s_inited || !s_tx_chan) return HAL_ERR_INIT;
    if (!pcm || samples == 0)   return HAL_ERR_DEV;

    /* Clear any stop request left over from a previous call so a stop
       issued between two Play() calls does not cancel this one. */
    s_stop_requested = false;

    size_t offset = 0;
    while (offset < samples) {
        if (s_stop_requested) {
            ESP_LOGI(TAG, "playback stopped at %u/%u samples",
                     (unsigned)offset, (unsigned)samples);
            return HAL_OK;   /* caller-requested stop is not a failure */
        }

        size_t chunk = samples - offset;
        if (chunk > HAL_AUDIO_CHUNK_SAMPLES) chunk = HAL_AUDIO_CHUNK_SAMPLES;

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            s_tx_chan,
            pcm + offset,
            chunk * sizeof(int16_t),
            &bytes_written,
            HAL_AUDIO_WRITE_TIMEOUT_MS);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write failed at %u/%u samples: %s",
                     (unsigned)offset, (unsigned)samples, esp_err_to_name(err));
            return (err == ESP_ERR_TIMEOUT) ? HAL_ERR_BUS : HAL_ERR_DEV;
        }
        if (bytes_written != chunk * sizeof(int16_t)) {
            ESP_LOGE(TAG, "short write: %u of %u bytes",
                     (unsigned)bytes_written, (unsigned)(chunk * sizeof(int16_t)));
            return HAL_ERR_DEV;
        }

        offset += chunk;
    }

    return HAL_OK;
}

hal_status_t HAL_Audio_Stop(void)
{
    /* Cooperative stop: sets the flag HAL_Audio_Play() checks between
       chunks. Does not touch the I2S channel -- doing so here would
       race with an in-flight i2s_channel_write() on another task. The
       caller is expected to see HAL_Audio_Play() return HAL_OK early,
       not an error; see the header comment on HAL_Audio_Stop(). */
    if (!s_inited) return HAL_ERR_INIT;
    s_stop_requested = true;
    ESP_LOGI(TAG, "stop requested");
    return HAL_OK;
}

hal_status_t HAL_Audio_SetVolume(uint8_t vol)
{
    /* MAX98357A has no digital volume control. Gain is set by a
     * resistor on the GAIN pin, decided at PCB design time.
     *
     * Constitution section 12 (honest-stub rule): returning HAL_OK
     * from a function that does nothing is forbidden. An earlier
     * version returned HAL_OK here; callers that checked the result
     * would conclude volume had been set. Return HAL_ERR_DEV so the
     * failure is visible.
     *
     * If a future revision adds PWM control of a variable-gain stage
     * (or replaces the amp with a digitally controlled one), this
     * function becomes real. Until then, it is a stub. */
    ESP_LOGW(TAG, "Volume control not supported on MAX98357A (use analog gain pin)");
    (void)vol;
    return HAL_ERR_DEV;
}
