#pragma once

#include <stdint.h>
#include <stdbool.h>
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
    uint32_t data; /* generic payload; replace with union later */
} event_t;

/* Initialize the event bus with an existing queue handle (kernel owns creation) */
void event_bus_init(QueueHandle_t queue);

/* Publish an event (non-blocking) */
void event_bus_publish(const event_t *evt);

/* Receive an event (blocking with timeout) */
bool event_bus_receive(event_t *evt, TickType_t timeout);