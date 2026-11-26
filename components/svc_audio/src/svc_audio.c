/*
 * svc_audio.c
 *
 * ChronoLink V4 OS -- adhan playback service (v1).
 *
 * One task, one owned event queue, one asset. Sequence per playback
 * (docs/audio-adhan-design.md sections 3.2 / 4.1; THIS ORDER IS LOAD-BEARING):
 *
 *   1. svc_config_get()  -> adhan_mode read LIVE, adhan_volume sampled once
 *   2. fopen(asset)      -> class 1 failure: log + skip, NO event
 *   3. decode buffers    -> resident, allocated on the first adhan
 *   4. dec_open()        -> failure: ERROR(DECODE,0); I2S is not up yet
 *   5. audio_init()      -> class 3a failure: ERROR(HAL_INIT,0), NO deinit
 *   6. first frame       -> class 2a/2a'' failure: ERROR(DECODE,0), deinit
 *   7. EVENT_AUDIO_STARTED, immediately before the first PCM write
 *   8. find-sync -> decode -> downmix -> volume-scale -> audio_play_pcm()
 *   9. silence flush -> audio_deinit() -> dec_close() -> fclose()
 *  10. EVENT_AUDIO_COMPLETED on a normal end
 *
 * TWO resources are acquired, and each is released if and only if it was
 * acquired:
 *
 *   decoder   MP3InitDecoder() -> MP3FreeDecoder()   step 4 (per playback)
 *   I2S       audio_init()     -> audio_deinit()      step 5 (per playback)
 *
 * Deinit on a failed init is explicitly forbidden (section 4.5) -- that is the
 * bug this file exists to not have. Freeing a decoder that was never opened,
 * or leaking one that was, is the same mistake one layer down.
 *
 * Services reach audio through the HIL adapter (components/hil/audio_api.c),
 * not the HAL directly, per docs/Architecture.md section 2 rule 2 and
 * docs/hil.md "Usage rules" 1. "I2S ownership" below therefore means
 * ownership of the init/deinit lifecycle, whichever adapter carries the call.
 *
 * Decoder: chmorgan/esp-libhelix-mp3 (RealNetworks Helix, fixed-point). There
 * is no floating point in the playback path -- Xtensa has no SIMD, and float
 * is slower than integer even on the S3's single-precision FPU. Every
 * libhelix symbol in this file lives inside dec_open() / dec_frame() /
 * dec_close(); nothing else knows the decoder exists.
 */
#include "svc_audio.h"

#include "audio_api.h"   /* HIL: audio_init / audio_play_pcm / audio_deinit  */
#include "svc_config.h"  /* svc_config_get, svc_config_adhan_mode_t          */
#include "event_bus.h"   /* EVENT_AUDIO_*, payload structs                   */
#include "time_events.h" /* prayer_time_event_t, PRAYER_KIND_ADHAN           */

#include "mp3dec.h"      /* libhelix-mp3: HMP3Decoder, MP3FrameInfo, ERR_*   */

#include "esp_log.h"
#include "esp_system.h"  /* esp_get_free_heap_size                           */
#include "esp_timer.h"   /* esp_timer_get_time, per-frame decode timing      */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "svc_audio";

/* The one asset. Per-prayer files are v2 (design section 1.2). */
#define AUDIO_ASSET_PATH "/storage/adhan.mp3"

/* Owned subscriber queue. Length 4 is enough for the worst case: one event
   being handled plus a handful that arrive during a 3-minute adhan, all of
   which are dropped as stale afterwards. */
#define AUDIO_QUEUE_LEN 4

/* Task stack. NOTHING large lives on this stack: libhelix's decoder state is
   heap (inside MP3InitDecoder) and the PCM / mono / compressed buffers are in
   the resident decode context. No decoder API here puts a scratch structure
   on the caller's stack. 6 KB is the starting point -- the "stack high-water"
   line logged after each playback decides the final value (design section 7).
   Do not tune it on a guess. */
#define AUDIO_TASK_STACK 6144

/* Above the 1 Hz services (tskIDLE_PRIORITY + 2): playback must refill the
   I2S DMA promptly when audio_play_pcm() returns. It only runs while an
   adhan is playing and blocks inside the I2S write otherwise, so it cannot
   starve the scheduler. */
#define AUDIO_TASK_PRIO (tskIDLE_PRIORITY + 3)
#define AUDIO_TASK_CORE 0

/* Compressed-data buffer, held in the decode context (heap) and refilled by
   memmove + fread -- never by reallocation, so the audio task never asks the
   allocator for anything while it is playing. */
#define AUDIO_READ_CHUNK 8192

/* Bytes that must be queued before MP3Decode() may run, while the file has
   not ended. MAINBUF_SIZE (mp3dec.h, 1940) is libhelix's own
   "largest possible main_data section" bound: up to 511 bytes of bit
   reservoir (9-bit main_data_begin) plus the largest Layer III frame
   (1428 slots), rounded up to 1940 for 4-byte alignment. With that much
   available the decoder can always resolve the reservoir reference it is
   about to unpack. Near EOF the rule is relaxed -- the loop tries with
   whatever is left, because a final frame can still be complete. */
#define AUDIO_MIN_DECODE_BYTES MAINBUF_SIZE

/* Interleaved 16-bit output for one MPEG1 Layer III frame:
   2 granules x 576 samples x 2 channels = 2304 shorts = 4608 bytes.
   MPEG2 / MPEG2.5 frames use one granule and report half of that. */
#define AUDIO_PCM_MAX_SHORTS (MAX_NSAMP * MAX_NGRAN * MAX_NCHAN)

/* Stereo is downmixed into this many mono samples (1152 for MPEG1). */
#define AUDIO_MONO_MAX_SHORTS (AUDIO_PCM_MAX_SHORTS / MAX_NCHAN)

/* Bounded recovery (MANDATORY, design section 3.3). libhelix can report
   "zero output samples" indefinitely on damaged input, and a false sync word
   inside ID3 data would otherwise spin the audio task forever. Two hard
   limits, either of which aborts as class 2c:
     - consecutive decode attempts that consume nothing (a stall), and
     - bytes scanned without producing a frame.
   Reaching either bound is a decode failure, never "end of stream".
   The attempt test is `stall > AUDIO_RESYNC_ATTEMPTS`: up to 8 consecutive
   non-productive decodes are tolerated, the 9th aborts. The byte test is
   `scanned > AUDIO_RESYNC_BYTES`: 64 KB may be scanned, the byte that takes it
   past 64 KB aborts. Either way the loop terminates -- that is the property
   that matters, and it is what stops libhelix's zero-sample loop. */
#define AUDIO_RESYNC_ATTEMPTS 8
#define AUDIO_RESYNC_BYTES    65536

/* Silence flush before audio_deinit() (design AMEND-2). Two full HAL DMA
   chunks of digital zeros; HAL_AUDIO_CHUNK_SAMPLES in
   chronolink_hal/src/hal_audio_max98357a.c is 1024 samples, so
   2 x 1024 satisfies "at least two full chunks" without this service having
   to include a HAL header (layering). The MAX98357A datasheet recommends
   ramping SDIN to zero before power-down; this is the v1 click/pop
   mitigation. */
#define AUDIO_SILENCE_CHUNK  1024
#define AUDIO_SILENCE_CHUNKS 2

/* Software volume ceiling: 0.70 * (adhan_volume / 100), in Q15. Volume 100 is
   therefore about -3.1 dBFS, NOT digital full scale (design AMEND-1):
   0 dBFS plus the board's 12 dB analog gain demands ~14.1 dBV while ~3.2 W
   into 4 ohm is ~11 dBV, so digital full scale would clip. round(0.70 *
   32768) = 22938. The multiplier is always < 32768, so no sample can leave
   int16_t range and the inner loop needs no clamp. */
#define AUDIO_VOLUME_MAX_Q15 22938

_Static_assert(AUDIO_READ_CHUNK > AUDIO_MIN_DECODE_BYTES,
               "read chunk must hold one full decode window plus a refill");
_Static_assert(sizeof(int16_t) == sizeof(short),
               "MP3Decode() writes shorts and audio_play_pcm() takes int16_t");
_Static_assert(AUDIO_PCM_MAX_SHORTS == 2304,
               "MPEG1 Layer III frame is 1152 samples x 2 channels");
_Static_assert(AUDIO_SILENCE_CHUNK * AUDIO_SILENCE_CHUNKS <= AUDIO_PCM_MAX_SHORTS,
               "silence flush reuses the PCM buffer");
_Static_assert(AUDIO_READ_CHUNK <= 32768,
               "MP3Decode()/MP3FindSyncWord() take an int byte count");

/* Everything one playback needs that is worth keeping between playbacks,
   allocated ONCE on the first adhan and kept resident (released only by a
   reboot). Buffers only: the decoder object is per-playback heap, so a
   damaged stream can never leave a stale bit reservoir behind for the next
   adhan. ~15 KB, of which 8 KB is the compressed-data buffer. A malloc/free
   pair per adhan would fragment the heap for no benefit, and the free-heap
   target in design section 7 already budgets for it. */
typedef struct {
    int16_t pcm[AUDIO_PCM_MAX_SHORTS];   /* 4608 B interleaved         */
    int16_t mono[AUDIO_MONO_MAX_SHORTS]; /* 2304 B, 1152 mono samples  */
    uint8_t in[AUDIO_READ_CHUNK];        /* 8 KB compressed            */
} audio_decode_ctx_t;

static const char *const kPrayerNames[SVC_CONFIG_PRAYER_COUNT] = {
    "Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"
};

static QueueHandle_t      s_queue   = NULL;  /* owned by this service      */
static TaskHandle_t       s_task    = NULL;
static bool               s_started = false;
static audio_decode_ctx_t *s_ctx    = NULL;  /* touched only by audio_task */

static const char *prayer_name(uint8_t id)
{
    return (id < SVC_CONFIG_PRAYER_COUNT) ? kPrayerNames[id] : "?";
}

/* --------------------------------------------------------------------------
 * Event publishing
 * ----------------------------------------------------------------------- */

/* 98 / 99: identical 16-byte payload. Telemetry only -- the void publish
   wrapper is correct here, a dropped lifecycle event needs no retry. */
static void publish_play_event(event_type_t type, uint8_t prayer_id,
                               uint8_t mode, int64_t scheduled_epoch)
{
    audio_play_payload_t payload;
    event_t evt;

    memset(&payload, 0, sizeof(payload));
    payload.prayer_id       = prayer_id;
    payload.mode            = mode;
    payload.scheduled_epoch = scheduled_epoch;

    event_init_payload(&evt, type, &payload, sizeof(payload));
    event_bus_publish(&evt);
}

/* 100. `partial` is 1 if and only if at least one PCM buffer had been written
   before the failure (design section 4, "partial semantics"). */
static void publish_error(uint8_t prayer_id, uint8_t stage, uint8_t partial,
                          int32_t err)
{
    audio_error_payload_t payload;
    event_t evt;

    memset(&payload, 0, sizeof(payload));
    payload.prayer_id = prayer_id;
    payload.stage     = stage;
    payload.partial   = partial;
    payload.err       = err;

    event_init_payload(&evt, EVENT_AUDIO_ERROR, &payload, sizeof(payload));
    event_bus_publish(&evt);
}

/* --------------------------------------------------------------------------
 * Sample processing
 * ----------------------------------------------------------------------- */

/* Q15 multiplier for one playback: 0.70 * volume / 100, computed once, with
   integer math only. Nothing in the per-sample path multiplies by 100 or
   touches a float. */
static int32_t volume_q15(uint8_t volume)
{
    if (volume >= 100) {
        return AUDIO_VOLUME_MAX_Q15;
    }
    return (int32_t)(((int32_t)volume * AUDIO_VOLUME_MAX_Q15) / 100);
}

/* Software volume: 0 = muted, 100 = 0.70 of full scale (see the Q15 constant).
   The MAX98357A has no digital gain (HAL_Audio_SetVolume() honestly returns
   HAL_ERR_DEV), so svc_audio scales the decoded int16 samples. `gain_q15` is
   computed ONCE at playback start, so a volume change made mid-playback takes
   effect at the NEXT adhan (design H3). */
static void apply_volume(int16_t *samples, size_t count, int32_t gain_q15)
{
    size_t i;

    if (gain_q15 == 0) {
        memset(samples, 0, count * sizeof(int16_t));
        return;
    }

    /* gain_q15 <= 22938 < 32768, so |sample * gain| >> 15 can never exceed
       the int16_t range: no clamp, no branch per sample. */
    for (i = 0; i < count; i++) {
        samples[i] = (int16_t)(((int32_t)samples[i] * gain_q15) >> 15);
    }
}

/* Stereo -> mono by averaging L/R BEFORE scaling (design section 3.1). The
   I2S channel is mono, and upload-time stereo rejection belongs to the
   deferred endpoint PR (section 1.3), so a stereo file must still play.
   Returns the number of mono samples written. */
static size_t downmix_stereo(const int16_t *interleaved, size_t frames,
                             int16_t *mono)
{
    size_t i;

    for (i = 0; i < frames; i++) {
        mono[i] = (int16_t)(((int32_t)interleaved[2 * i] +
                             (int32_t)interleaved[2 * i + 1]) / 2);
    }
    return frames;
}

/* --------------------------------------------------------------------------
 * Decoder seam -- every libhelix symbol in this file lives in these three
 * helpers. Nothing above them knows which MP3 decoder is linked in.
 * ----------------------------------------------------------------------- */

/* MP3InitDecoder() allocates its own state on the heap (MP3DecInfo plus the
   frame-header / side-info / scale-factor / Huffman / dequant / IMDCT /
   subband buffers). NULL therefore means "allocation failed"; libhelix
   reports nothing finer, and SVC_AUDIO_ERR_NO_MEM is what it maps to. */
static HMP3Decoder dec_open(void)
{
    return MP3InitDecoder();
}

static void dec_close(HMP3Decoder dec)
{
    if (dec) {
        MP3FreeDecoder(dec);
    }
}

typedef enum {
    DEC_FRAME_OK = 0,     /* one frame decoded into the PCM buffer        */
    DEC_FRAME_NEED_DATA,  /* more bytes needed, or the reservoir fills    */
    DEC_FRAME_ERROR,      /* bad frame here: resync, bounded by the caller */
} dec_frame_status_t;

typedef struct {
    dec_frame_status_t status;
    size_t consumed;      /* bytes of input the decoder moved past      */
    int    shorts;        /* interleaved 16-bit samples written to pcm  */
    int    channels;      /* 1 or 2                                     */
    int    sample_rate;   /* Hz                                         */
    int    rc;            /* raw libhelix return, for the log line      */
} dec_frame_result_t;

/* Decode at most one frame. libhelix consumes input by advancing *inbuf and
   decrementing *bytesLeft, so the caller learns the frame length from
   `consumed` -- which is also how it skips garbage and ID3 data.
 *
 * The sync search is explicit instead of being left to MP3Decode: the
 * component README documents this loop, because an ID3 tag routinely contains
 * 0xFF 0xFB and a false sync there makes MP3Decode return an error rather
 * than the frame. Skipping to the next real sync word and retrying recovers;
 * treating that error as end of stream would truncate the adhan at the tag.
 *
 * `pcm` must hold AUDIO_PCM_MAX_SHORTS shorts: useSize = 0 tells libhelix the
 * output buffer is its full MAX_NSAMP * MAX_NGRAN * MAX_NCHAN size.
 */
static void dec_frame(HMP3Decoder dec, uint8_t *in, size_t avail,
                      int16_t *pcm, dec_frame_result_t *out)
{
    /* Pinned libhelix 1.0.3 -- libhelix-mp3/pub/mp3dec.h, the header the
       component manager actually fetches:
           int MP3Decode(HMP3Decoder, unsigned char **inbuf, int *bytesLeft,
                         short *outbuf, int useSize);
           int MP3FindSyncWord(unsigned char *buf, int nBytes);
       NON-const pointers and `int` counts. Upstream master differs
       (const unsigned char **, size_t *), and the two are not interchangeable:
       copying master's signature here is an incompatible-pointer error under
       -Werror. The fetched header wins -- do not "restore" const. */
    unsigned char *p = in;      /* advanced by MP3Decode; the buffer is ours */
    const int avail_i = (int)avail;
    int left = avail_i;         /* int *, not size_t *                      */
    int offset;
    int rc;

    memset(out, 0, sizeof(*out));

    offset = MP3FindSyncWord(p, avail_i);
    if (offset < 0) {
        /* No sync word anywhere in the window. Keep the last 3 bytes: the
           next sync word can straddle the read boundary, and resuming the
           search before them would drop a frame. */
        out->consumed = (avail > 3) ? (avail - 3) : 0;
        out->status   = DEC_FRAME_NEED_DATA;
        return;
    }
    if (offset > 0) {
        /* Garbage (or ID3 data) before the sync word: skip it, retry. */
        out->consumed = (size_t)offset;
        out->status   = DEC_FRAME_NEED_DATA;
        return;
    }

    rc = MP3Decode(dec, &p, &left, (short *)pcm, 0);

    out->rc       = rc;
    out->consumed = (left >= 0 && left <= avail_i) ? (size_t)(avail_i - left) : 0;

    if (rc == ERR_MP3_NONE) {
        MP3FrameInfo info;

        memset(&info, 0, sizeof(info));
        MP3GetLastFrameInfo(dec, &info);

        out->shorts      = info.outputSamps;
        out->channels    = info.nChans;
        out->sample_rate = info.samprate;
        out->status      = (info.outputSamps > 0) ? DEC_FRAME_OK
                                                  : DEC_FRAME_NEED_DATA;
        return;
    }

    /* Underflow is NOT fatal:
         ERR_MP3_INDATA_UNDERFLOW   the frame's bytes are not all here yet;
         ERR_MP3_MAINDATA_UNDERFLOW normal while the bit reservoir fills over
                                    the first frames of a stream;
         ERR_MP3_FREE_BITRATE_SYNC  the bitrate needs one more frame header.
       All three resolve by supplying more data. */
    if (rc == ERR_MP3_INDATA_UNDERFLOW ||
        rc == ERR_MP3_MAINDATA_UNDERFLOW ||
        rc == ERR_MP3_FREE_BITRATE_SYNC) {
        out->status = DEC_FRAME_NEED_DATA;
        return;
    }

    /* Anything else (INVALID_FRAMEHEADER, INVALID_SIDEINFO, the other
       INVALID_*, OUT_OF_MEMORY, NULL_POINTER) means "no frame here". It is
       reported as a bad frame, not as end of stream: the caller resyncs past
       it and the bounded-recovery counters decide when to give up. */
    out->status = DEC_FRAME_ERROR;
}

/* --------------------------------------------------------------------------
 * Decode context -- buffers only; the decoder object is dec_open()'s
 * ----------------------------------------------------------------------- */

/* Allocate the resident buffers on first use. Returns NULL on allocation
   failure; the caller maps that to stage=DECODE, partial=0 and, because no
   I2S resource has been acquired at that point, no deinit. */
static audio_decode_ctx_t *decode_ctx_get(void)
{
    uint32_t heap_before;

    if (s_ctx) {
        return s_ctx;
    }

    heap_before = esp_get_free_heap_size();
    s_ctx = (audio_decode_ctx_t *)malloc(sizeof(audio_decode_ctx_t));
    if (!s_ctx) {
        ESP_LOGE(TAG, "decode buffer alloc failed (%u bytes)",
                 (unsigned)sizeof(audio_decode_ctx_t));
        return NULL;
    }

    ESP_LOGI(TAG, "decode buffers: %u bytes (pcm %u B, mono %u B, in %u B); "
                  "free heap %u -> %u (delta %d)",
             (unsigned)sizeof(audio_decode_ctx_t),
             (unsigned)sizeof(s_ctx->pcm), (unsigned)sizeof(s_ctx->mono),
             (unsigned)sizeof(s_ctx->in), (unsigned)heap_before,
             (unsigned)esp_get_free_heap_size(),
             (int)((int32_t)heap_before - (int32_t)esp_get_free_heap_size()));
    return s_ctx;
}

/* --------------------------------------------------------------------------
 * Playback
 * ----------------------------------------------------------------------- */

/* Decode the open asset and push PCM to the HIL.
 *
 * Publishes EVENT_AUDIO_STARTED immediately before the first write and
 * EVENT_AUDIO_ERROR on a class 2 / 3b failure. Never touches init, deinit or
 * the two resource handles: the caller owns those.
 *
 * Returns true on a NORMAL completion -- end of file, the SHORT sample cap, or
 * a trailing partial frame that cannot be completed -- and false when an error
 * event was published (the caller then flushes, deinits and frees).
 */
static bool stream_playback(FILE *f, audio_decode_ctx_t *ctx, HMP3Decoder dec,
                            uint8_t prayer_id, uint8_t mode, uint8_t volume,
                            int64_t scheduled_epoch)
{
    const size_t short_cap =
        (size_t)SVC_AUDIO_SAMPLE_RATE_HZ * (size_t)SVC_AUDIO_SHORT_DURATION_S;
    const int32_t gain_q15 = volume_q15(volume);
    size_t   pos       = 0;   /* read position inside ctx->in             */
    size_t   used      = 0;   /* valid bytes in ctx->in                   */
    size_t   played    = 0;   /* mono samples successfully written        */
    size_t   scanned   = 0;   /* bytes consumed since the last good frame */
    unsigned stall     = 0;   /* consecutive non-productive decodes       */
    unsigned frames    = 0;   /* decoded frames, for the timing stats     */
    int64_t  sum_us    = 0;
    int64_t  max_us    = 0;
    bool     eof       = false;
    bool     started   = false;
    bool     saw_frame = false;

    for (;;) {
        dec_frame_result_t res;
        int16_t *dst;
        size_t   per_channel;
        size_t   n_samples;
        int64_t  t0;
        int64_t  dt_us;

        /* ---- refill ---------------------------------------------------
           Only while the file has not ended: at EOF the residual is all we
           will ever get, and decoding is allowed to try with less than
           AUDIO_MIN_DECODE_BYTES so a complete final frame still plays. */
        if (!eof && (used - pos) < AUDIO_MIN_DECODE_BYTES) {
            size_t got;

            memmove(ctx->in, ctx->in + pos, used - pos);
            used -= pos;
            pos = 0;

            got = fread(ctx->in + used, 1, sizeof(ctx->in) - used, f);
            if (got == 0) {
                /* ferror() is the wear-levelling / I/O failure class 2b is
                   about. A clean fread()==0 with feof() set is simply the
                   end of the asset (design section 3.2). */
                if (ferror(f)) {
                    ESP_LOGE(TAG, "read failed after %u samples -- aborting",
                             (unsigned)played);
                    publish_error(prayer_id, SVC_AUDIO_STAGE_READ,
                                  (uint8_t)(played > 0), SVC_AUDIO_ERR_READ);
                    return false;
                }
                eof = true;
            }
            used += got;
        }

        if (used == pos) {
            if (!saw_frame) {
                /* Opened, but nothing decodable: zero-byte or non-MP3 file. */
                ESP_LOGE(TAG, "no MP3 frame in the asset");
                publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE, 0,
                              SVC_AUDIO_ERR_DECODE);
                return false;
            }
            break;                        /* clean end of the asset */
        }

        /* ---- one frame ------------------------------------------------- */
        t0 = esp_timer_get_time();
        dec_frame(dec, ctx->in + pos, used - pos, ctx->pcm, &res);
        dt_us = esp_timer_get_time() - t0;
        frames++;
        sum_us += dt_us;
        if (dt_us > max_us) {
            max_us = dt_us;
        }

        if (res.status != DEC_FRAME_OK) {
            /* Progress accounting for the bounded recovery: skipping bytes
               (ID3 tag, inter-frame garbage, a bad frame) is progress;
               consuming nothing is a stall. */
            if (res.consumed > 0) {
                pos     += res.consumed;
                scanned += res.consumed;
                stall    = 0;
            } else {
                stall++;
                if (res.status == DEC_FRAME_ERROR) {
                    /* Forced resync: step over the byte that produced the
                       pseudo-sync so the next pass cannot find it again. */
                    pos++;
                    scanned++;
                }
            }

            if (stall > AUDIO_RESYNC_ATTEMPTS || scanned > AUDIO_RESYNC_BYTES) {
                if (!saw_frame) {
                    ESP_LOGE(TAG, "no MP3 frame in %u byte(s) -- aborting",
                             (unsigned)scanned);
                } else {
                    ESP_LOGE(TAG, "decoder gave up after %u byte(s) without a "
                                  "frame (rc=%d) -- aborting",
                             (unsigned)scanned, res.rc);
                }
                publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE,
                              (uint8_t)(played > 0), SVC_AUDIO_ERR_DECODE);
                return false;
            }

            if (eof && (pos >= used ||
                        (res.status == DEC_FRAME_NEED_DATA &&
                         res.consumed == 0))) {
                /* Nothing more will arrive and the decoder cannot make
                   further progress (NEED_DATA with nothing consumed, or the
                   window is exhausted). A tail that is not a complete frame
                   is a truncated asset, not an error: everything the file
                   contained has played. */
                if (!saw_frame) {
                    ESP_LOGE(TAG, "no decodable MP3 frame in the asset");
                    publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE, 0,
                                  SVC_AUDIO_ERR_DECODE);
                    return false;
                }
                ESP_LOGW(TAG, "asset ends with %u byte(s) that do not form a "
                              "complete frame -- ignored",
                         (unsigned)(used - pos));
                break;
            }
            continue;
        }

        /* ---- a decoded frame ------------------------------------------- */
        if (res.consumed == 0) {
            /* Defensive: a decoded frame always consumes input. Without this
               guard a zero-length advance would replay the same frame for the
               whole adhan. */
            ESP_LOGE(TAG, "decoder produced audio without consuming input");
            publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE,
                          (uint8_t)(played > 0), SVC_AUDIO_ERR_DECODE);
            return false;
        }

        saw_frame = true;
        stall     = 0;      /* a frame resets both recovery counters */
        scanned   = 0;
        pos      += res.consumed;

        if (res.channels < 1 || res.shorts <= 0 ||
            (size_t)res.shorts > AUDIO_PCM_MAX_SHORTS) {
            ESP_LOGE(TAG, "decoder reported %d sample(s) in %d channel(s) -- "
                          "aborting", res.shorts, res.channels);
            publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE,
                          (uint8_t)(played > 0), SVC_AUDIO_ERR_DECODE);
            return false;
        }

        /* The HAL's I2S channel is fixed at 44.1 kHz and there is no
           resampler (design D2), so any other rate is a failure -- the
           message names the file's rate so the operator can re-encode it. */
        if (res.sample_rate != SVC_AUDIO_SAMPLE_RATE_HZ) {
            ESP_LOGE(TAG, "decoded sample rate %d Hz != %d Hz (no resampler) "
                          "-- aborting",
                     res.sample_rate, (int)SVC_AUDIO_SAMPLE_RATE_HZ);
            publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE,
                          (uint8_t)(played > 0), SVC_AUDIO_ERR_DECODE);
            return false;
        }

        per_channel = (size_t)res.shorts / (size_t)res.channels;
        if (res.channels == 2) {
            n_samples = downmix_stereo(ctx->pcm, per_channel, ctx->mono);
            dst = ctx->mono;
        } else {
            n_samples = per_channel;
            dst = ctx->pcm;
        }

        /* SHORT: at most SVC_AUDIO_SHORT_DURATION_S of DECODED samples (not
           wall-clock). Reaching the cap is a normal completion, never an
           error (design M1). */
        if (mode == SVC_AUDIO_MODE_SHORT) {
            if (played >= short_cap) {
                ESP_LOGI(TAG, "SHORT cap reached (%u samples) -- complete",
                         (unsigned)played);
                break;
            }
            if (n_samples > short_cap - played) {
                n_samples = short_cap - played;
            }
        }

        apply_volume(dst, n_samples, gain_q15);

        if (!started) {
            /* 98: the decoder produced a valid, correctly-rated frame and
               playback is about to begin. */
            publish_play_event(EVENT_AUDIO_STARTED, prayer_id, mode,
                               scheduled_epoch);
            started = true;
        }

        if (!audio_play_pcm(dst, n_samples)) {
            /* Class 3b: I2S write timeout / DMA stall. */
            ESP_LOGE(TAG, "audio_play_pcm failed after %u samples -- aborting",
                     (unsigned)played);
            publish_error(prayer_id, SVC_AUDIO_STAGE_HAL_WRITE,
                          (uint8_t)(played > 0), SVC_AUDIO_ERR_WRITE);
            return false;
        }

        played += n_samples;
    }

    if (played == 0) {
        /* Reached only by a stream that decoded frames but never wrote a
           sample (all frames empty). Class 2a in event terms: opened, and no
           audio resulted. */
        ESP_LOGE(TAG, "asset produced no decodable audio samples");
        publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE, 0,
                      SVC_AUDIO_ERR_DECODE);
        return false;
    }

    /* Per-frame decode cost against the real-time budget: 44100 / 1152 =
       ~38.3 frames per second, i.e. ~26 ms per frame available at 44.1 kHz.
       The mean must stay far below that or the I2S DMA starves. */
    ESP_LOGI(TAG, "%s decode: %u frame(s), mean %lld us, max %lld us "
                  "(budget ~26000 us/frame)",
             prayer_name(prayer_id), frames,
             (long long)(frames ? (sum_us / (int64_t)frames) : 0),
             (long long)max_us);

    return true;
}

/* AMEND-2: ramp SDIN to zero before the amplifier is powered down. Writes two
   full HAL DMA chunks of digital zeros. Reuses the resident PCM buffer, so
   nothing large touches the task stack, and a failed write is only logged:
   the deinit that follows is what actually silences the amp. Only ever called
   after audio_init() succeeded, so ctx is non-NULL by construction. */
static void silence_flush(audio_decode_ctx_t *ctx)
{
    int i;

    memset(ctx->pcm, 0, AUDIO_SILENCE_CHUNK * sizeof(int16_t));
    for (i = 0; i < AUDIO_SILENCE_CHUNKS; i++) {
        if (!audio_play_pcm(ctx->pcm, AUDIO_SILENCE_CHUNK)) {
            ESP_LOGW(TAG, "silence flush write failed -- continuing to deinit");
            return;
        }
    }
}

/* One adhan request, start to finish. The I2S lifecycle and the decoder
   lifecycle both live entirely inside this function, so no other task can
   ever race a deinit against a write (design section 5-I2). */
static void play_adhan(const prayer_time_event_t *req)
{
    svc_config_t cfg;
    audio_decode_ctx_t *ctx;
    HMP3Decoder dec;
    FILE *f;
    uint8_t mode;
    uint8_t volume;
    uint32_t heap_before;
    uint32_t heap_after;
    bool completed;

    /* (1) Config is read LIVE, once per playback: adhan_mode every time
       (I1 -- a mosque flipping OFF -> FULL must hear the next adhan) and
       adhan_volume sampled here, never re-read in the decode loop (H3). One
       svc_config_get() gives both from a single consistent snapshot. */
    if (svc_config_get(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "%s adhan due, but svc_config is unavailable -- skipped",
                 prayer_name(req->prayer_id));
        return;                        /* no I2S, no event */
    }

    mode = (uint8_t)cfg.adhan_mode;
    if (mode == SVC_AUDIO_MODE_OFF) {
        ESP_LOGI(TAG, "%s adhan due -- adhan_mode is OFF, nothing to play",
                 prayer_name(req->prayer_id));
        return;                        /* no I2S, no event */
    }
    if (mode != SVC_AUDIO_MODE_FULL && mode != SVC_AUDIO_MODE_SHORT) {
        ESP_LOGW(TAG, "unknown adhan_mode %u -- treating as FULL",
                 (unsigned)mode);
        mode = SVC_AUDIO_MODE_FULL;
    }
    volume = (cfg.adhan_volume > 100) ? 100 : cfg.adhan_volume;

    ESP_LOGI(TAG, "%s adhan: mode=%s volume=%u free_heap=%u",
             prayer_name(req->prayer_id),
             (mode == SVC_AUDIO_MODE_SHORT) ? "SHORT" : "FULL",
             (unsigned)volume, (unsigned)esp_get_free_heap_size());

    /* (2) Open. Class 1: log + skip, NO event and nothing acquired yet. */
    f = fopen(AUDIO_ASSET_PATH, "rb");
    if (!f) {
        ESP_LOGW(TAG, "%s is absent or unreadable -- adhan skipped",
                 AUDIO_ASSET_PATH);
        return;
    }

    /* (3) Resident buffers, allocated on the first adhan only. Neither this
       nor the decoder open below touches I2S, so a failure here leaves I2S
       down and there is nothing to deinit. */
    ctx = decode_ctx_get();
    if (!ctx) {
        publish_error(req->prayer_id, SVC_AUDIO_STAGE_DECODE, 0,
                      SVC_AUDIO_ERR_NO_MEM);
        fclose(f);
        return;
    }

    /* (4) A FRESH decoder per playback: no bit reservoir, no free-format
       bitrate and no Huffman state can leak from the previous adhan. */
    heap_before = esp_get_free_heap_size();
    dec = dec_open();
    if (!dec) {
        /* libhelix allocates its state inside MP3InitDecoder(), so a NULL
           handle is an allocation failure. I2S was never initialised. */
        ESP_LOGE(TAG, "MP3InitDecoder failed -- adhan skipped, I2S stays down");
        publish_error(req->prayer_id, SVC_AUDIO_STAGE_DECODE, 0,
                      SVC_AUDIO_ERR_NO_MEM);
        fclose(f);
        return;
    }
    heap_after = esp_get_free_heap_size();
    ESP_LOGI(TAG, "decoder opened: free heap %u -> %u (delta %d bytes)",
             (unsigned)heap_before, (unsigned)heap_after,
             (int)((int32_t)heap_before - (int32_t)heap_after));

    /* (5) I2S comes up only now, on this task, for this playback. */
    if (!audio_init()) {
        /* Class 3a: init FAILED, so there is nothing to deinit -- but the
           decoder WAS acquired and must be released. */
        ESP_LOGE(TAG, "audio_init failed -- adhan skipped, I2S stays down");
        publish_error(req->prayer_id, SVC_AUDIO_STAGE_HAL_INIT, 0,
                      SVC_AUDIO_ERR_INIT);
        dec_close(dec);
        fclose(f);
        return;
    }

    /* (6)-(8) Decode and write. stream_playback returns false only when it
       published EVENT_AUDIO_ERROR itself. */
    completed = stream_playback(f, ctx, dec, req->prayer_id, mode, volume,
                                req->scheduled_epoch);

    /* (9) Shut both resources down in the safest order: zeros into the amp
       first (AMEND-2), then the I2S channel, then the decoder. Reached only
       because init succeeded above, so this deinit is always paired. */
    silence_flush(ctx);

    if (!audio_deinit()) {
        ESP_LOGW(TAG, "audio_deinit failed -- Init refuses to run until a "
                      "retry succeeds");
    }

    dec_close(dec);
    fclose(f);

    /* (10) Normal end: both resources are down, so COMPLETED is published with
       the device back in its idle state. Error paths published their own
       event above. */
    if (completed) {
        publish_play_event(EVENT_AUDIO_COMPLETED, req->prayer_id, mode,
                           req->scheduled_epoch);
        ESP_LOGI(TAG, "%s adhan complete", prayer_name(req->prayer_id));
    }

    /* Measurement for design section 7 -- do not tune the stack on a guess.
       uxTaskGetStackHighWaterMark(), NOT the ...2 variant: ESP-IDF's
       FreeRTOSConfig.h sets INCLUDE_uxTaskGetStackHighWaterMark to 1 and leaves
       INCLUDE_uxTaskGetStackHighWaterMark2 at its 0 default (FreeRTOS.h), so
       ...2 is not declared at all in this build. The base variant reports
       configSTACK_DEPTH_TYPE in stack units, and this port's portSTACK_TYPE is
       uint8_t -- which is why xTaskCreatePinnedToCore() above also takes bytes
       -- so the count is already BYTES, not words. */
    ESP_LOGI(TAG, "stack high-water: %u bytes free of %d",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), AUDIO_TASK_STACK);
}

/* Drop everything that queued up while we were playing.
 *
 * Playback blocks this task for the whole adhan, so the kernel dispatcher
 * keeps forwarding EVENT_PRAYER_TIME_REACHED copies into s_queue (length 4).
 * Those requests are stale by the time we return: playing one would start a
 * second adhan minutes late. Duplicate requests are LOGGED AND DROPPED,
 * before any stop decision -- v1 has no pre-emption (design sections 3.1,
 * 4.2). Runs after every handled event, i.e. as soon as the device is idle
 * again. */
static void drain_stale_requests(void)
{
    event_t evt;

    while (xQueueReceive(s_queue, &evt, 0) == pdTRUE) {
        ESP_LOGW(TAG, "dropped adhan request that arrived during playback "
                      "(event type %d)", (int)evt.type);
    }
}

/* Filter and dispatch one received event. */
static void handle_event(const event_t *evt)
{
    prayer_time_event_t req;

    if (evt->type != EVENT_PRAYER_TIME_REACHED) {
        ESP_LOGD(TAG, "ignoring event type %d", (int)evt->type);
        return;
    }

    memcpy(&req, evt->data, sizeof(req));

    /* M2: adhan tier only. PRAYER_KIND_JAMAT is reserved and never fired in
       v1; anything else is a bug upstream, not a reason to play audio. */
    if (req.kind != (uint8_t)PRAYER_KIND_ADHAN) {
        ESP_LOGD(TAG, "prayer %u kind %u is not an adhan -- ignored",
                 (unsigned)req.prayer_id, (unsigned)req.kind);
        return;
    }

    play_adhan(&req);
}

static void audio_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "task started (core %d, stack %d bytes, prio %d)",
             AUDIO_TASK_CORE, AUDIO_TASK_STACK, AUDIO_TASK_PRIO);

    for (;;) {
        event_t evt;

        if (xQueueReceive(s_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;                  /* defensive: only a stop frees us */
        }

        handle_event(&evt);

        /* Back to idle: nothing that arrived during playback may play. */
        drain_stale_requests();
    }
}

esp_err_t svc_audio_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    /* Section 7 instrumentation: the build's real sizes, so the human can
       write measured numbers back into the doc after the first flash. */
    ESP_LOGI(TAG, "libhelix: PCM %d shorts/frame, buffers %u bytes, "
                  "decode window %d bytes, asset %s",
             AUDIO_PCM_MAX_SHORTS, (unsigned)sizeof(audio_decode_ctx_t),
             (int)AUDIO_MIN_DECODE_BYTES, AUDIO_ASSET_PATH);

    s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(event_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(audio_task, "svc_audio", AUDIO_TASK_STACK,
                                NULL, AUDIO_TASK_PRIO, &s_task,
                                AUDIO_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "failed to create task");
        s_task = NULL;
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* The one subscription this service needs: an owned queue, one type. The
       kernel queue is never drained here (M2). A failure means no adhan can
       ever arrive, so log it loudly -- but do NOT fail start(): the contract
       is task creation only, and main.c must keep booting (section 4.3). */
    if (!event_bus_subscribe(s_queue, EVENT_PRAYER_TIME_REACHED)) {
        ESP_LOGE(TAG, "subscribe(EVENT_PRAYER_TIME_REACHED) failed -- "
                      "no adhan will ever be received");
    }

    s_started = true;
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}
