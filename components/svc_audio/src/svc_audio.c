/*
 * svc_audio.c
 *
 * ChronoLink V4 OS -- adhan playback service (v1).
 *
 * One task, one owned event queue, one asset. Sequence per playback
 * (docs/audio-adhan-design.md section 3.2; THIS ORDER IS LOAD-BEARING):
 *
 *   1. svc_config_get()   -> adhan_mode read LIVE, adhan_volume sampled once
 *   2. fopen(asset)       -> class 1 failure: log + skip, NO event
 *   3. audio_init()       -> class 3a failure: ERROR(HAL_INIT,0), NO deinit
 *   4. first decode       -> class 2a failure: ERROR(DECODE,0), deinit
 *   5. EVENT_AUDIO_STARTED, immediately before the first PCM write
 *   6. fread -> decode -> downmix -> volume-scale -> audio_play_pcm()
 *   7. audio_deinit(), then EVENT_AUDIO_COMPLETED on a normal end
 *
 * Deinit is called IF AND ONLY IF init succeeded, on this task, before
 * returning to idle. Deinit on a failed init is explicitly forbidden
 * (section 4.5) -- that is the bug this file exists to not have.
 *
 * Services reach audio through the HIL adapter (components/hil/audio_api.c),
 * not the HAL directly, per docs/Architecture.md section 2 rule 2 and
 * docs/hil.md "Usage rules" 1. "I2S ownership" below therefore means
 * ownership of the init/deinit lifecycle, whichever adapter carries the call.
 */
#include "svc_audio.h"

#include "audio_api.h"   /* HIL: audio_init / audio_play_pcm / audio_deinit  */
#include "svc_config.h"  /* svc_config_get, svc_config_adhan_mode_t          */
#include "event_bus.h"   /* EVENT_AUDIO_*, payload structs                   */
#include "time_events.h" /* prayer_time_event_t, PRAYER_KIND_ADHAN           */

#include "minimp3.h"

#include "esp_log.h"
#include "esp_system.h"  /* esp_get_free_heap_size                           */

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

/* Task stack. mp3dec_decode_frame() puts an `mp3dec_scratch_t` on the
   CALLER's stack: bs_t + maindata[511+2304] + gr_info[4] + grbuf[2][576]
   floats + scf[40] + syn[33][64] floats + ist_pos = ~16.3 KB. 24 KB leaves
   ~8 KB for ESP_LOG, stdio and the decode call chain. Confirm with the
   high-water log after each playback; do not shrink this on a guess. */
#define AUDIO_TASK_STACK 24576

/* Above the 1 Hz services (tskIDLE_PRIORITY + 2): playback must refill the
   I2S DMA promptly when audio_play_pcm() returns. It only runs while an
   adhan is playing and blocks inside the I2S write otherwise, so it cannot
   starve the scheduler. */
#define AUDIO_TASK_PRIO (tskIDLE_PRIORITY + 3)
#define AUDIO_TASK_CORE 0

/* Compressed-data buffer. Must stay well above AUDIO_FRAME_WINDOW so a
   refill can always make progress. */
#define AUDIO_READ_CHUNK 8192

/* Never hand minimp3 a buffer that could end mid-frame: decode only while at
   least this many bytes are queued. 2304 is minimp3's
   MAX_FREE_FORMAT_FRAME_SIZE (its own "more than ISO spec's" bound), which
   is above the largest Layer III frame at 44.1 kHz (1046 B at 320 kbps);
   +4 covers the header minimp3 scans past. */
#define AUDIO_FRAME_WINDOW (2304 + 4)

_Static_assert(AUDIO_READ_CHUNK > (2 * AUDIO_FRAME_WINDOW),
               "read chunk must hold more than one frame window");

/* The HIL takes int16_t PCM. MINIMP3_FLOAT_OUTPUT must stay undefined. */
_Static_assert(sizeof(mp3d_sample_t) == sizeof(int16_t),
               "minimp3 built with float output -- audio_play_pcm needs int16");

/* Everything one playback needs, allocated ONCE on the first adhan and kept
   resident (released only by a reboot). A ~22 KB malloc/free per adhan would
   fragment the heap for no benefit, and the ~130 KB free-heap target in the
   design's section 7 already budgets for it. */
typedef struct {
    mp3dec_t dec;                                     /* ~6.7 KB           */
    int16_t  pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];      /* 2304 interleaved  */
    int16_t  mono[MINIMP3_MAX_SAMPLES_PER_FRAME / 2]; /* 1152 mono samples */
    uint8_t  in[AUDIO_READ_CHUNK];                    /* 8 KB compressed   */
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

/* Software volume: 0 = muted, 100 = unity. The MAX98357A has no digital gain
   (HAL_Audio_SetVolume() honestly returns HAL_ERR_DEV), so svc_audio scales
   the decoded int16 samples. `volume` is sampled ONCE at playback start, so a
   change made mid-playback takes effect at the NEXT adhan (design H3). */
static void apply_volume(int16_t *samples, size_t count, uint8_t volume)
{
    size_t i;

    if (volume == 100) {
        return;                       /* unity: no multiply, no rounding */
    }

    if (volume == 0) {
        memset(samples, 0, count * sizeof(int16_t));
        return;
    }

    /* 100/100 scaling cannot exceed int16_t range, so no clamp is needed. */
    for (i = 0; i < count; i++) {
        samples[i] = (int16_t)(((int32_t)samples[i] * (int32_t)volume) / 100);
    }
}

/* Stereo -> mono by averaging L/R before scaling (design section 3.1). The
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
 * Decoder context
 * ----------------------------------------------------------------------- */

/* Allocate the decode context on first use. Returns NULL on allocation
   failure; the caller maps that to stage=DECODE, partial=0. */
static audio_decode_ctx_t *decode_ctx_get(void)
{
    if (s_ctx) {
        return s_ctx;
    }

    s_ctx = (audio_decode_ctx_t *)malloc(sizeof(audio_decode_ctx_t));
    if (!s_ctx) {
        ESP_LOGE(TAG, "decode context alloc failed (%u bytes)",
                 (unsigned)sizeof(audio_decode_ctx_t));
        return NULL;
    }

    ESP_LOGI(TAG, "decode context allocated: %u bytes (free heap %u)",
             (unsigned)sizeof(audio_decode_ctx_t),
             (unsigned)esp_get_free_heap_size());
    return s_ctx;
}

/* --------------------------------------------------------------------------
 * Playback
 * ----------------------------------------------------------------------- */

/* Decode the open asset and push PCM to the HIL.
 *
 * Publishes EVENT_AUDIO_STARTED immediately before the first write and
 * EVENT_AUDIO_ERROR on a class 2 / 3b failure. Never touches init or deinit:
 * the caller owns both.
 *
 * Returns true on a NORMAL completion -- end of file, the SHORT sample cap,
 * or a trailing partial frame at EOF -- and false when an error event was
 * published (the caller then deinits and goes idle).
 */
static bool stream_playback(FILE *f, audio_decode_ctx_t *ctx, uint8_t prayer_id,
                            uint8_t mode, uint8_t volume,
                            int64_t scheduled_epoch)
{
    const size_t short_cap =
        (size_t)SVC_AUDIO_SAMPLE_RATE_HZ * (size_t)SVC_AUDIO_SHORT_DURATION_S;
    size_t pos       = 0;   /* read position inside ctx->in                */
    size_t used      = 0;   /* valid bytes in ctx->in                      */
    size_t played    = 0;   /* mono samples successfully written           */
    bool   eof       = false;
    bool   need_data = false;
    bool   started   = false;

    for (;;) {
        mp3dec_frame_info_t info;
        int16_t *dst;
        size_t n_samples;
        int samples;

        /* ---- refill ------------------------------------------------ */
        if (!eof && (need_data || (used - pos) < AUDIO_FRAME_WINDOW)) {
            size_t got;

            memmove(ctx->in, ctx->in + pos, used - pos);
            used -= pos;
            pos = 0;
            need_data = false;

            if (used == sizeof(ctx->in)) {
                /* A full read buffer that still holds no usable frame: this
                   is not MP3 (or is corrupt). */
                ESP_LOGE(TAG, "no MP3 frame in %u bytes -- aborting",
                         (unsigned)used);
                publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE,
                              (uint8_t)(played > 0), SVC_AUDIO_ERR_DECODE);
                return false;
            }

            got = fread(ctx->in + used, 1, sizeof(ctx->in) - used, f);
            if (got == 0) {
                /* ferror() is the wear-levelling / I/O failure class 2b is
                   about. A clean fread()==0 with feof() set is simply the
                   end of the asset. */
                if (ferror(f)) {
                    ESP_LOGE(TAG, "read failed after %u samples -- aborting",
                             (unsigned)played);
                    publish_error(prayer_id, SVC_AUDIO_STAGE_READ,
                                  (uint8_t)(played > 0), SVC_AUDIO_ERR_READ);
                    return false;
                }
                eof = true;
                if (used == 0) {
                    break;                  /* clean end of the asset */
                }
            }
            used += got;
        }

        /* ---- decode ------------------------------------------------ */
        memset(&info, 0, sizeof(info));
        samples = mp3dec_decode_frame(&ctx->dec, ctx->in + pos,
                                      (int)(used - pos), ctx->pcm, &info);

        if (samples <= 0) {
            /* Frame skipped (ID3v2 / trailing tag) or no complete frame yet.
               minimp3 reports the bytes it walked past in info.frame_bytes. */
            if ((size_t)info.frame_bytes > 0 &&
                (size_t)info.frame_bytes < (used - pos)) {
                pos += (size_t)info.frame_bytes;
                continue;
            }

            if (eof) {
                if (played == 0) {
                    /* Class 2a: opened but no first sync frame. */
                    ESP_LOGE(TAG, "no decodable MP3 frame in the asset");
                    publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE, 0,
                                  SVC_AUDIO_ERR_DECODE);
                    return false;
                }
                ESP_LOGW(TAG, "trailing %u byte(s) at EOF are not a frame -- "
                              "ignored", (unsigned)(used - pos));
                break;
            }

            /* Need more data. If minimp3 rejected the whole buffer, keep one
               frame window so a frame straddling the read boundary is not
               lost, then read again. Progress is guaranteed: the buffer is
               larger than two windows. */
            if ((size_t)info.frame_bytes == (used - pos) &&
                (used - pos) > AUDIO_FRAME_WINDOW) {
                pos = used - AUDIO_FRAME_WINDOW;
            }
            need_data = true;
            continue;
        }

        if (info.frame_bytes <= 0) {
            /* Defensive: a decoded frame always reports its length. */
            ESP_LOGE(TAG, "decoder returned %d samples with no frame length",
                     samples);
            publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE,
                          (uint8_t)(played > 0), SVC_AUDIO_ERR_DECODE);
            return false;
        }
        pos += (size_t)info.frame_bytes;

        /* The HAL's I2S channel is fixed at 44.1 kHz and there is no
           resampler (design D2), so any other rate is a failure. */
        if (info.hz != SVC_AUDIO_SAMPLE_RATE_HZ) {
            ESP_LOGE(TAG, "decoded sample rate %d Hz != %d Hz -- aborting",
                     info.hz, (int)SVC_AUDIO_SAMPLE_RATE_HZ);
            publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE,
                          (uint8_t)(played > 0), SVC_AUDIO_ERR_DECODE);
            return false;
        }

        if (info.channels == 2) {
            n_samples = downmix_stereo(ctx->pcm, (size_t)samples, ctx->mono);
            dst = ctx->mono;
        } else {
            n_samples = (size_t)samples;
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

        apply_volume(dst, n_samples, volume);

        if (!started) {
            /* 98: init succeeded, playback is about to begin. */
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
        /* A zero-byte asset (or one that is only ID3 tags) reaches the clean
           EOF above with nothing decoded. Class 2a: opened, no first frame. */
        ESP_LOGE(TAG, "asset produced no decodable audio samples");
        publish_error(prayer_id, SVC_AUDIO_STAGE_DECODE, 0,
                      SVC_AUDIO_ERR_DECODE);
        return false;
    }

    return true;
}

/* One adhan request, start to finish. The I2S lifecycle lives entirely
   inside this function so no other task can ever race a deinit against a
   write (design section 5-I2). */
static void play_adhan(const prayer_time_event_t *req)
{
    svc_config_t cfg;
    audio_decode_ctx_t *ctx;
    FILE *f;
    uint8_t mode;
    uint8_t volume;
    bool completed;

    /* (1)+(2) Config is read LIVE, once per playback: adhan_mode every time
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

    /* (3) Open. Class 1: log + skip, NO event and nothing initialised yet. */
    f = fopen(AUDIO_ASSET_PATH, "rb");
    if (!f) {
        ESP_LOGW(TAG, "%s is absent or unreadable -- adhan skipped",
                 AUDIO_ASSET_PATH);
        return;
    }

    /* (4) I2S comes up only now, on this task, for this playback. */
    if (!audio_init()) {
        /* Class 3a: init FAILED, so there is nothing to deinit. */
        ESP_LOGE(TAG, "audio_init failed -- adhan skipped, I2S stays down");
        publish_error(req->prayer_id, SVC_AUDIO_STAGE_HAL_INIT, 0,
                      SVC_AUDIO_ERR_INIT);
        fclose(f);
        return;
    }

    /* (5) Decoder state. Allocation failure maps to stage=DECODE,
       partial=0; init succeeded, so this path DOES deinit below. */
    ctx = decode_ctx_get();
    if (!ctx) {
        publish_error(req->prayer_id, SVC_AUDIO_STAGE_DECODE, 0,
                      SVC_AUDIO_ERR_NO_MEM);
        audio_deinit();
        fclose(f);
        return;
    }

    /* Per-playback reset of the bit reservoir / free-format state. */
    mp3dec_init(&ctx->dec);

    completed = stream_playback(f, ctx, req->prayer_id, mode, volume,
                                req->scheduled_epoch);
    fclose(f);

    /* (7) Deinit happens iff init succeeded, always on this task, before the
       service returns to idle. */
    if (!audio_deinit()) {
        ESP_LOGW(TAG, "audio_deinit failed -- Init refuses to run until a "
                      "retry succeeds");
    }

    /* (8) Normal end: deinit is already done, so COMPLETED is published with
       the device back down. Error paths published their own event above. */
    if (completed) {
        publish_play_event(EVENT_AUDIO_COMPLETED, req->prayer_id, mode,
                           req->scheduled_epoch);
        ESP_LOGI(TAG, "%s adhan complete", prayer_name(req->prayer_id));
    }

    /* Measurement for design section 7 -- do not tune the stack on a guess. */
    ESP_LOGI(TAG, "stack high-water: %u bytes free of %d",
             (unsigned)uxTaskGetStackHighWaterMark2(NULL), AUDIO_TASK_STACK);
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
    ESP_LOGI(TAG, "minimp3: sizeof(mp3dec_t)=%u, decode context=%u, "
                  "max samples/frame=%d, asset=%s",
             (unsigned)sizeof(mp3dec_t), (unsigned)sizeof(audio_decode_ctx_t),
             (int)MINIMP3_MAX_SAMPLES_PER_FRAME, AUDIO_ASSET_PATH);

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
