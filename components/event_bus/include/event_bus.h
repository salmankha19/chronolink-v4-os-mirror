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
} event_type_t;

typedef struct {
    event_type_t type;
    uint8_t data[sizeof(boot_event_payload_u)];
} event_t;

_Static_assert(sizeof(((event_t*)0)->data) >= sizeof(boot_event_payload_u),
               "event_t.data too small for boot_event_payload_u");

void event_bus_init(QueueHandle_t queue);
void event_bus_publish(const event_t *evt);
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

    if (!evt || !payload || payload_len == 0) {
        return;
    }

    evt->type = type;
    copy_len = payload_len > sizeof(evt->data) ? sizeof(evt->data) : payload_len;
    memcpy(evt->data, payload, copy_len);
}