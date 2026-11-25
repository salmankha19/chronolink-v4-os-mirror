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
bool audio_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* HIL_AUDIO_API_H */
