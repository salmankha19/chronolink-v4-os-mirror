#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

typedef struct {
    uint32_t id;
    uint32_t value;
} os_state_msg_t;

void state_manager_init(QueueHandle_t queue);
void state_manager_dispatch(const os_state_msg_t *msg);
