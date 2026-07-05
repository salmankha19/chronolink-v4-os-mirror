#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "boot_events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    EVENT_NONE = 0,
    EVENT_TIME_UPDATE,
    EVENT_SENSOR_UPDATE,
    EVENT_PRAYER_UPDATE,
    EVENT_STATE_CHANGE,
    EVENT_UI_REQUEST,
} event_type_t;

typedef struct {
    event_type_t type;
    uint8_t data[sizeof(boot_event_payload_u)];
} event_t;

_Static_assert(sizeof(((event_t*)0)->data) >= sizeof(boot_event_payload_u), "event_t.data too small for boot_event_payload_u");

/* Initialize the event bus with an existing queue handle (kernel owns creation) */
void event_bus_init(QueueHandle_t queue);

/* Publish an event (non-blocking) */
void event_bus_publish(const event_t *evt);

/* Receive an event (blocking with timeout) */
bool event_bus_receive(event_t *evt, TickType_t timeout);

static inline void event_init_payload(event_t *evt, uint32_t type, const void *payload, size_t payload_len)
{
    size_t copy_len;

    if (!evt || !payload || payload_len == 0) {
        return;
    }

    evt->type = (event_type_t)type;
    copy_len = payload_len > sizeof(evt->data) ? sizeof(evt->data) : payload_len;
    memcpy(evt->data, payload, copy_len);
}
