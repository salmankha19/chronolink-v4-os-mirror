/*
 * hal_audio_max98357a.c
 *
 * ChronoLink V4 OS — MAX98357A I2S Class-D Amp
 *
 * No I2C/SPI control. I2S TX plus the SD_MODE power gate (GPIO1).
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
   PDL_PIN_I2S_DOUT=21, PDL_PIN_AUDIO_SD_MODE=1 (GPIO1) on this PCB.

   An earlier #ifndef fallback block lived here with values 26/25/27,
   routed through CONFIG_BOARD_AUDIO_*. It was unreachable: board_pins.h
   already pulls in pdl_pins.h, which unconditionally defines every
   PDL_PIN_I2S_* and PDL_PIN_AUDIO_SD_MODE macro, so the fallback
   branch never compiled in. The 26/25/27 values did not match this
   PCB anyway. Removed in PR 3. */

/* MAX98357A tON is 7-7.5 ms (datasheet). Releasing shutdown and starting
   BCLK inside that window loses the first milliseconds of the adhan, so
   HAL_Audio_AmpSetEnabled(true) waits this long before returning. */
#define HAL_AUDIO_AMP_TON_GUARD_MS  10

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_inited = false;
static volatile bool s_stop_requested = false;

/* Amplifier state, owned by this driver. s_amp_enabled is true only while
   this driver has driven SD_MODE high; it is the single test HAL_Audio_*
   uses to decide whether it may drive the pin low again. That is the third
   pair of the discipline svc_audio applies one layer up: disable iff we
   enabled. s_amp_gpio_ready caches the one-time gpio_config() so a second
   playback does not re-run it. */
static bool s_amp_enabled = false;
static bool s_amp_gpio_ready = false;

/* Drive SD_MODE and maintain s_amp_enabled. See hal_audio.h for the full
   contract; this is the only place that writes the pin after board init. */
hal_status_t HAL_Audio_AmpSetEnabled(bool en)
{
#if defined(PDL_PIN_AUDIO_SD_MODE) && PDL_PIN_AUDIO_SD_MODE >= 0
    /* Idempotent, and deliberately does NOT restart the tON guard: a second
       enable (svc_audio's explicit call followed by HAL_Audio_Init()'s
       internal one) must not add another 10 ms. */
    if (en == s_amp_enabled) {
        return HAL_OK;
    }

    if (!s_amp_gpio_ready) {
        gpio_config_t sd_conf = {
            .pin_bit_mask = (1ULL << PDL_PIN_AUDIO_SD_MODE),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t cfg_err = gpio_config(&sd_conf);
        if (cfg_err != ESP_OK) {
            /* Do not fall through to gpio_set_level(): driving a pin whose
               configuration failed is not a defined operation. */
            ESP_LOGE(TAG, "SD_MODE GPIO%d config failed: %s",
                     (int)PDL_PIN_AUDIO_SD_MODE, esp_err_to_name(cfg_err));
            return HAL_ERR_DEV;
        }
        s_amp_gpio_ready = true;
    }

    esp_err_t lvl_err = gpio_set_level((gpio_num_t)PDL_PIN_AUDIO_SD_MODE,
                                       en ? 1 : 0);
    if (lvl_err != ESP_OK) {
        ESP_LOGE(TAG, "SD_MODE GPIO%d set %d failed: %s",
                 (int)PDL_PIN_AUDIO_SD_MODE, en ? 1 : 0,
                 esp_err_to_name(lvl_err));
        return HAL_ERR_DEV;
    }

    s_amp_enabled = en;

    if (en) {
        /* The pin is high now; the amplifier needs tON before it will accept
           data. This runs before any I2S channel exists, on the caller's
           task (svc_audio's), so the wait costs a delay, not a stall. */
        TickType_t guard = pdMS_TO_TICKS(HAL_AUDIO_AMP_TON_GUARD_MS);
        if (guard == 0) {
            guard = 1;   /* tick rate too coarse to express 10 ms */
        }
        vTaskDelay(guard);
        ESP_LOGI(TAG, "SD_MODE GPIO%d high -- amplifier enabled (waited %d ms for tON)",
                 (int)PDL_PIN_AUDIO_SD_MODE, HAL_AUDIO_AMP_TON_GUARD_MS);
    } else {
        ESP_LOGI(TAG, "SD_MODE GPIO%d low -- amplifier in shutdown",
                 (int)PDL_PIN_AUDIO_SD_MODE);
    }
    return HAL_OK;
#else
    /* No control pin on this board: the hardware strap decides the amplifier's
       state and firmware cannot change it. Returning HAL_OK would claim the
       request took effect, which Constitution section 12 forbids. */
    ESP_LOGW(TAG, "SD_MODE not under firmware control (pin %d) -- amp state unchanged",
             (int)PDL_PIN_AUDIO_SD_MODE);
    (void)en;
    return HAL_ERR_DEV;
#endif
}

/* Return the amplifier to shutdown iff this driver enabled it. Both
   HAL_Audio_Deinit() exit paths call this, so "returning from Deinit leaves
   the amplifier off" holds even when the teardown is the idle one. */
static hal_status_t amp_release(void)
{
#if defined(PDL_PIN_AUDIO_SD_MODE) && PDL_PIN_AUDIO_SD_MODE >= 0
    if (!s_amp_enabled) {
        return HAL_OK;
    }
    return HAL_Audio_AmpSetEnabled(false);
#else
    return HAL_OK;
#endif
}

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

    /* Amplifier first, and before any channel exists: an I2S channel with the
       amplifier in shutdown is silence, so Init is not "ready" until the amp
       is out of shutdown. The >= 10 ms tON wait happens inside the setter,
       i.e. still before the first BCLK edge. Idempotent, so svc_audio's
       explicit enable just before this call is normally a no-op. */
    #if defined(PDL_PIN_AUDIO_SD_MODE) && PDL_PIN_AUDIO_SD_MODE >= 0
        hal_status_t amp_status = HAL_Audio_AmpSetEnabled(true);
        if (amp_status != HAL_OK) {
            /* Nothing was allocated yet, so there is no channel to unwind.
               Deinit is still safe to call later: amp_release() runs on its
               idle path too. */
            ESP_LOGE(TAG, "amplifier enable failed (%d) -- I2S not started",
                     (int)amp_status);
            return amp_status;
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
       Init expects the TX channel slot to be free.

       The amplifier is a separate resource from the channel, so it is
       released on BOTH exit paths below, and only if this driver enabled
       it (amp_release). */
    if (!s_inited && !s_tx_chan) {
        /* Idle path. Still release the amplifier: a failed HAL_Audio_Init()
           may have enabled it before i2s_new_channel() was reached. */
        hal_status_t idle_amp = amp_release();
        if (idle_amp != HAL_OK) {
            ESP_LOGW(TAG, "amplifier shutdown failed on idle deinit (%d)",
                     (int)idle_amp);
        }
        return idle_amp;
    }

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

    /* Clocks are gone (or were never up). Put the amplifier back into
       shutdown. svc_audio normally does this itself, right after the silence
       flush and before this call, so the usual outcome here is a no-op; the
       call is what keeps the invariant true for any other caller. */
    hal_status_t amp_status = amp_release();
    if (amp_status != HAL_OK && status == HAL_OK) {
        status = amp_status;
    }

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
