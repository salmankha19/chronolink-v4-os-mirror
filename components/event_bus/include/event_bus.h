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
    EVENT_TIME_UPDATE      = 1,
    EVENT_SENSOR_UPDATE    = 2,
    EVENT_PRAYER_UPDATE    = 3,
    EVENT_STATE_CHANGE     = 4,
    EVENT_UI_REQUEST       = 5,

    /* Boot lifecycle events -- formerly EVENT_BOOT_* in boot_events.h,
       which collided with EVENT_STATE_CHANGE (4) and EVENT_UI_REQUEST (5).
       Names kept; values moved to the 32..63 range. */
    EVENT_BOOT_START       = 32,
    EVENT_BOOT_READY       = 33,
    EVENT_BOOT_ERROR       = 34,
    EVENT_BOOT_HEALTH      = 35,
    EVENT_BOOT_FAIL        = 36,
    EVENT_BOOT_STAGE       = 37,
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