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

#ifndef PDL_PIN_AUDIO_SD_MODE
#define PDL_PIN_AUDIO_SD_MODE (-1)
#endif

#ifndef PDL_PIN_I2S_BCLK
#define PDL_PIN_I2S_BCLK 26
#endif
#ifndef PDL_PIN_I2S_WS
#define PDL_PIN_I2S_WS   25
#endif
#ifndef PDL_PIN_I2S_DOUT
#define PDL_PIN_I2S_DOUT 27
#endif

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_inited = false;

hal_status_t HAL_Audio_Init(void)
{
    if (s_inited) return HAL_OK;

    /* Optional SD_MODE (shutdown) pin */
    #if defined(PDL_PIN_AUDIO_SD_MODE) && PDL_PIN_AUDIO_SD_MODE >= 0
	    gpio_config_t sd_mode_conf = {
	        .pin_bit_mask = (1ULL << PDL_PIN_AUDIO_SD_MODE),
	        .mode = GPIO_MODE_OUTPUT,
	        .pull_up_en = GPIO_PULLUP_DISABLE,
	        .pull_down_en = GPIO_PULLDOWN_DISABLE,
	        .intr_type = GPIO_INTR_DISABLE,
	    };
	    gpio_config(&sd_mode_conf);
	    gpio_set_level(PDL_PIN_AUDIO_SD_MODE, 1);
	#endif

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);

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

    if (i2s_channel_init_std_mode(s_tx_chan, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed");
        return HAL_ERR_INIT;
    }
    i2s_channel_enable(s_tx_chan);

    s_inited = true;
    ESP_LOGI(TAG, "MAX98357A I2S ready");
    return HAL_OK;
}

hal_status_t HAL_Audio_Deinit(void)
{
    if (!s_inited) return HAL_OK;
    if (s_tx_chan) {
        i2s_channel_disable(s_tx_chan);
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }

    #if PDL_PIN_AUDIO_SD_MODE >= 0
        gpio_set_level(PDL_PIN_AUDIO_SD_MODE, 0);
    #endif

    s_inited = false;
    return HAL_OK;
}

hal_status_t HAL_Audio_Play(const int16_t *pcm, size_t samples)
{
    if (!s_inited || !pcm || samples == 0) return HAL_ERR_INIT;
    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx_chan, pcm, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    return (err == ESP_OK) ? HAL_OK : HAL_ERR_DEV;
}

hal_status_t HAL_Audio_SetVolume(uint8_t vol)
{
    /* MAX98357A has no digital volume control.
       Gain is set by resistor on GAIN pin. */
    ESP_LOGW(TAG, "Volume control not supported on MAX98357A (use analog gain pin)");
    (void)vol;
    return HAL_OK;  // ← This tells the caller "success" even though no volume control exists
}