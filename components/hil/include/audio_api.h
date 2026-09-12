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

/* NOT IMPLEMENTED: there is no MP3 decoder anywhere in this codebase.
   chronolink_hal's HAL_Audio_Play() only accepts raw PCM. Playing an
   .mp3 file needs a decoder (e.g. Helix MP3, minimp3) decoding to PCM
   in chunks, fed into audio_play_pcm() -- that's real, un-started work,
   not a one-line wrapper. Returns false always until that decoder
   exists (likely living in svc_audio, calling audio_play_pcm() per
   decoded chunk, not here). */
bool audio_play_mp3(const char *filepath);

bool audio_set_volume(uint8_t percent_0_100);
bool audio_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* HIL_AUDIO_API_H */
