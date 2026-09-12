/*
 * audio_api.c
 *
 * Thin adapter over chronolink_hal's HAL_Audio_* (hal_audio.h /
 * hal_audio_max98357a.c). See audio_api.h for why audio_play_mp3()
 * isn't implemented -- no MP3 decoder exists anywhere in this
 * codebase yet, and faking it here would hide that.
 */
#include "audio_api.h"
#include "hal_audio.h"
#include "esp_log.h"

static const char *TAG = "AUDIO_API";
static volatile bool s_is_playing = false;

bool audio_init(void)
{
    return HAL_Audio_Init() == HAL_OK;
}

bool audio_deinit(void)
{
    return HAL_Audio_Deinit() == HAL_OK;
}

bool audio_play_pcm(const int16_t *pcm, size_t samples)
{
    if (!pcm || samples == 0) return false;
    s_is_playing = true;
    hal_status_t hs = HAL_Audio_Play(pcm, samples);
    s_is_playing = false;
    return hs == HAL_OK;
}

bool audio_play_mp3(const char *filepath)
{
    (void)filepath;
    ESP_LOGE(TAG, "audio_play_mp3: no MP3 decoder exists in this codebase yet -- "
                  "see audio_api.h. Use audio_play_pcm() with pre-decoded PCM "
                  "for now, or a WAV/raw Adhan asset instead of MP3.");
    return false;
}

bool audio_set_volume(uint8_t percent_0_100)
{
    /* HAL_Audio_SetVolume() itself is a documented no-op on the
       MAX98357A -- there's no digital volume control on this amp, gain
       is set by a resistor on its GAIN pin. Calling it here doesn't
       lie about that; it just forwards to what the HDL driver already
       honestly returns. */
    return HAL_Audio_SetVolume(percent_0_100) == HAL_OK;
}

bool audio_is_playing(void)
{
    return s_is_playing;
}
