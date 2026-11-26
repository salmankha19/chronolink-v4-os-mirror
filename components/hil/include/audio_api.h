#ifndef HIL_AUDIO_API_H
#define HIL_AUDIO_API_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool audio_init(void);
bool audio_deinit(void);

/* Play a mono or stereo 16-bit PCM buffer (blocking). This is the
   actual capability chronolink_hal has today. */
bool audio_play_pcm(const int16_t *pcm, size_t samples);

/* NOT IMPLEMENTED HERE: there is no decoder in the HIL. chronolink_hal's
   HAL_Audio_Play() only accepts raw PCM, so an .mp3 file has to be decoded
   to PCM in chunks and fed to audio_play_pcm(). That decoder now exists:
   chmorgan/esp-libhelix-mp3 (managed component, fixed-point; declared in
   svc_audio's idf_component.yml) is driven by
   components/svc_audio, which is the ONE owner of the audio lifecycle
   (audio_init -> audio_play_pcm* -> audio_deinit, called on its own task).
   This function stays a documented failure so no caller believes the HIL
   can play a file by itself. */
bool audio_play_mp3(const char *filepath);

bool audio_set_volume(uint8_t percent_0_100);

/* Amplifier power (MAX98357A SD_MODE, GPIO1). true = out of shutdown, false =
 * shutdown (0.6 uA, high-Z). Blocks for the HAL's >= 10 ms tON guard on the
 * enable transition, so call it on a task, before audio_init().
 *
 * Returns false when the amplifier's power state cannot be changed: this board
 * has no firmware-controlled SD_MODE (PDL_PIN_AUDIO_SD_MODE < 0) or the GPIO
 * failed. A caller that needs sound must treat false as a hard failure — the
 * adhan would be silent.
 *
 * Only svc_audio calls this, and it disables the amplifier iff it enabled it
 * (the third resource pair, alongside decoder and I2S). */
bool audio_amp_set_enabled(bool en);
bool audio_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* HIL_AUDIO_API_H */
