#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

/* Forward declaration of the UIState struct tag only (no typedef).
   This avoids conflicting with the canonical typedef in ui_state.h. */
struct UIState;

typedef struct {
    uint32_t id;
    uint32_t value;
} os_state_msg_t;

/* Initialize the state manager with an existing queue handle.
   The kernel (core_os) creates the queue and passes the handle here.
   Idempotent and safe to call multiple times. */
void state_manager_init(QueueHandle_t queue);
