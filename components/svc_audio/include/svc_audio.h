/*
 * svc_audio.h
 *
 * ChronoLink V4 OS -- adhan playback service (v1)
 *
 * Plays ONE asset, /storage/adhan.mp3 (MP3, 44.1 kHz; mono plays as-is and a
 * stereo stream is downmixed to mono, because the I2S channel is mono), when
 * svc_scheduler publishes EVENT_PRAYER_TIME_REACHED (97) with
 * prayer_time_event_t.kind == PRAYER_KIND_ADHAN. Design and rationale:
 * docs/audio-adhan-design.md.
 *
 * Ownership rules that callers and maintainers must not "simplify":
 *   - ONE task owns the audio device for the whole playback. The I2S
 *     channel is initialised lazily at playback request and deinitialised
 *     before the task goes back to idle: there is no idle I2S instance.
 *   - adhan_mode and adhan_volume are read live from svc_config at every
 *     playback, never cached at start. A flip OFF -> FULL mid-day is heard
 *     on the next adhan.
 *   - SHORT mode plays at most SVC_AUDIO_SHORT_DURATION_S seconds of the
 *     SAME asset, counted in decoded samples. No second file, no
 *     resampling, no audio_stop().
 *
 * Events published (payloads in event_bus.h):
 *   98  EVENT_AUDIO_STARTED    audio_play_payload_t, before the first PCM write
 *   99  EVENT_AUDIO_COMPLETED  audio_play_payload_t, after deinit, normal end
 *   100 EVENT_AUDIO_ERROR      audio_error_payload_t, on any class 2/3 failure
 * A missing / unreadable asset emits NOTHING (log + skip): a mosque with no
 * adhan configured is not a fault.
 */
#pragma once

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHORT mode cap in seconds, applied to the single asset. Counted in decoded
   samples (samples / 44100), not wall-clock time, so a slow or fast decode
   still yields the same amount of audio. Reaching the cap is a NORMAL
   completion, never an error. */
#define SVC_AUDIO_SHORT_DURATION_S 10

/* Sample rate the HAL's I2S channel is configured for (hard-coded 44100 in
   HAL_Audio_Init). The decoder rejects anything else rather than resampling
   (design D2). */
#define SVC_AUDIO_SAMPLE_RATE_HZ 44100

/* audio_play_payload_t.mode -- mirrors svc_config_adhan_mode_t. 0 (OFF) never
   reaches a payload: an OFF request returns before I2S is touched. */
#define SVC_AUDIO_MODE_OFF   0
#define SVC_AUDIO_MODE_FULL  1
#define SVC_AUDIO_MODE_SHORT 2

/* audio_error_payload_t.stage. Stage 0 (OPEN) is defined for parity with
   svc_network_stage_t but is never emitted in v1: open-time failures are
   class 1 (log + skip, no event). */
#define SVC_AUDIO_STAGE_OPEN      0
#define SVC_AUDIO_STAGE_READ      1
#define SVC_AUDIO_STAGE_DECODE    2
#define SVC_AUDIO_STAGE_HAL_INIT  3
#define SVC_AUDIO_STAGE_HAL_WRITE 4

/* audio_error_payload_t.err. The HIL audio adapter returns bool, so the
   granular HAL status is not visible here; the HAL/HIL log line carries the
   detail. These codes name the failing call. */
#define SVC_AUDIO_ERR_INIT   (-1) /* audio_init() returned false              */
#define SVC_AUDIO_ERR_WRITE  (-2) /* audio_play_pcm() returned false          */
#define SVC_AUDIO_ERR_DECODE (-3) /* libhelix: no frame / corrupt / wrong rate */
#define SVC_AUDIO_ERR_NO_MEM (-4) /* alloc failed: buffers or MP3InitDecoder  */
#define SVC_AUDIO_ERR_READ   (-5) /* fread failed mid-playback                */

/*
 * Start the service: create the owned event queue and the svc_audio task.
 *
 * Returns ESP_OK on successful task creation ONLY. A missing
 * /storage/adhan.mp3, missing configuration and decoder-allocation failure
 * are handled at PLAYBACK time (they do not fail start): the device keeps a
 * working clock and logs the reason when the adhan is due.
 *
 * Idempotent. Non-fatal for the caller: main.c logs a failure and continues,
 * exactly as for svc_scheduler_start(). There is no rollback-gate logic in
 * this service -- svc_http_start(), which runs first, owns that decision.
 */
esp_err_t svc_audio_start(void);

#ifdef __cplusplus
}
#endif
