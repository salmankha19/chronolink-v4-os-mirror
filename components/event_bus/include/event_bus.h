#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "boot_events.h"

typedef enum {
    EVENT_NONE = 0,

    /* Application / service events */
    EVENT_TIME_UPDATE        = 1,
    EVENT_SENSOR_UPDATE      = 2,
    EVENT_PRAYER_UPDATE      = 3,
    EVENT_STATE_CHANGE       = 4,
    EVENT_UI_REQUEST         = 5,
    /* Published by svc_time when the local calendar date changes (or the
       effective timezone offset changes, e.g. DST transition). Consumers
       that cache times keyed on date/offset should recompute. */
    EVENT_TIME_DATE_CHANGED  = 6,

    /* Boot lifecycle events -- formerly EVENT_BOOT_* in boot_events.h,
       which collided with EVENT_STATE_CHANGE (4) and EVENT_UI_REQUEST (5).
       Names kept; values moved to the 32..63 range. */
    EVENT_BOOT_START       = 32,
    EVENT_BOOT_READY       = 33,
    EVENT_BOOT_ERROR       = 34,
    EVENT_BOOT_HEALTH      = 35,
    EVENT_BOOT_FAIL        = 36,
    EVENT_BOOT_STAGE       = 37,

    /* Network lifecycle events (svc_network). Range 64..95 reserved for
       network-related events. svc_http / svc_ota should use the 112..127
       band; the 96..111 band is reserved for prayer / audio scheduling. */
    EVENT_NETWORK_STARTING     = 64,
    EVENT_NETWORK_CONNECTED    = 65,  /* associated + got IP */
    EVENT_NETWORK_DISCONNECTED = 66,
    EVENT_NETWORK_ERROR        = 67,
    /* Prayer engine events (svc_prayer_engine_service). 96..111 is the
       prayer / audio scheduling band. */
    EVENT_PRAYER_TIMES_UPDATED = 96,
    /* Published by svc_scheduler when a prayer time is reached exactly once
       per prayer per day (epoch guard, see scheduler_logic.c). Payload:
       prayer_time_event_t in time_events.h. */
    EVENT_PRAYER_TIME_REACHED  = 97,
    /* svc_audio lifecycle. 98..100 sit inside the prayer / audio band
       (96..111); the 112..127 band belongs to svc_http / svc_ota and must
       not be used here. Payloads: audio_play_payload_t (98, 99) and
       audio_error_payload_t (100), defined below this enum. */
    EVENT_AUDIO_STARTED        = 98,
    EVENT_AUDIO_COMPLETED      = 99,
    EVENT_AUDIO_ERROR          = 100,
} event_type_t;

typedef struct {
    event_type_t type;
    uint8_t data[sizeof(boot_event_payload_u)];
} event_t;

_Static_assert(sizeof(((event_t*)0)->data) >= sizeof(boot_event_payload_u),
               "event_t.data too small for boot_event_payload_u");

/* ------------------------------------------------------------------------
 * svc_audio payloads (EVENT_AUDIO_STARTED 98 / EVENT_AUDIO_COMPLETED 99 /
 * EVENT_AUDIO_ERROR 100)
 *
 * Three events, two shapes: 98 and 99 share audio_play_payload_t, 100 uses
 * audio_error_payload_t. Consumers cast evt->data to the struct for the
 * event type they handle. Keep these in sync with components/svc_audio.
 * --------------------------------------------------------------------- */

/* 98 STARTED / 99 COMPLETED -- 16 B (int64_t forces 8-byte alignment). */
typedef struct {
    uint8_t prayer_id;
    uint8_t mode;            /* 1=FULL, 2=SHORT (0=OFF never starts) */
    uint8_t _reserved[6];    /* explicit pad: 2 + 6 = 8, then i64 at offset 8 */
    int64_t scheduled_epoch; /* original scheduler epoch, for correlation
                                with prayer_time_event_t */
} audio_play_payload_t;      /* sizeof == 16 */

/* 100 ERROR -- 8 B. `partial` carries the partial-playback indicator. */
typedef struct {
    uint8_t prayer_id;
    uint8_t stage;   /* 0=OPEN,1=READ,2=DECODE,3=HAL_INIT,4=HAL_WRITE
                        (values in svc_audio.h, SVC_AUDIO_STAGE_*) */
    uint8_t partial; /* 1 iff at least one PCM buffer was written before the
                        failure, else 0 (see svc_audio.c) */
    uint8_t _reserved;
    int32_t err;     /* SVC_AUDIO_ERR_* / HAL status, see svc_audio.h */
} audio_error_payload_t;     /* sizeof == 8 */

_Static_assert(sizeof(audio_play_payload_t) <= sizeof(((event_t*)0)->data),
               "audio_play_payload_t exceeds event_t.data size");
_Static_assert(sizeof(audio_play_payload_t) == 16,
               "audio_play_payload_t must be 16 bytes");
_Static_assert(sizeof(audio_error_payload_t) <= sizeof(((event_t*)0)->data),
               "audio_error_payload_t exceeds event_t.data size");
_Static_assert(sizeof(audio_error_payload_t) == 8,
               "audio_error_payload_t must be 8 bytes");

void event_bus_init(QueueHandle_t queue);
void event_bus_publish(const event_t *evt);

/* Checked variant of event_bus_publish() (CR #1).
 *
 * Returns true when the event was accepted into the kernel event queue
 * (including the "queue full -> evict oldest -> retry once" path), and
 * false when it was NOT enqueued: evt == NULL, no queue registered, or
 * the retry after eviction also failed. Callers that must not lose the
 * event (svc_scheduler's EVENT_PRAYER_TIME_REACHED) use this and retry
 * on false. All other callers keep using the void wrapper below.
 *
 * event_bus_publish() is now exactly (void)event_bus_publish_checked(). */
bool event_bus_publish_checked(const event_t *evt);

bool event_bus_receive(event_t *evt, TickType_t timeout);

/* Subscriber fan-out (PR 5b-1).
 *
 * event_bus_subscribe() registers ONE (queue, type) rule: a copy of every
 * event of that type is sent to q by the kernel dispatcher through
 * event_bus_dispatch(). Intended to be called during init, before events
 * start flowing -- it is NOT thread-safe against event_bus_dispatch().
 *
 * Returns true on success, and also if (q, type) is already registered
 * (idempotent). Returns false when q is NULL or the subscriber table is
 * full (EVENT_BUS_MAX_SUBSCRIBERS). */
bool event_bus_subscribe(QueueHandle_t q, event_type_t type);

/* Companion of event_bus_publish() / event_bus_receive(): the kernel
 * dispatcher calls this once per dequeued event to forward a copy to every
 * subscriber queue registered for evt->type. Never blocks -- a full
 * subscriber queue drops that copy. Safe with a NULL argument (no-op).
 * Do not register the kernel's own queue (the dispatcher's source): the
 * forwarded copy would be dequeued and forwarded again, forever. */
void event_bus_dispatch(const event_t *evt);

static inline void event_init_payload(event_t *evt, event_type_t type,
                                      const void *payload, size_t payload_len)
{
    size_t copy_len;

    if (!evt) {
        return;
    }

    /* CR S5: set the type before the payload guard. An event with no
       payload is still a valid event and needs its type; the old order
       returned first and left whatever the caller's stack held. */
    evt->type = type;

    if (!payload || payload_len == 0) {
        return;
    }

    /* Clear the tail first so bytes beyond payload_len are zeroes, not
       stale stack contents forwarded to subscribers. Never writes past
       sizeof(evt->data). */
    memset(evt->data, 0, sizeof(evt->data));

    copy_len = payload_len > sizeof(evt->data) ? sizeof(evt->data) : payload_len;
    memcpy(evt->data, payload, copy_len);
}