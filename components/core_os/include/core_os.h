#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "boot_events.h"
#include "state_manager.h"

/* Kernel init: create queues, timers, tasks, but do NOT start scheduler here */
void core_os_init(boot_flags_t boot_flags);

/* Kernel start: create tasks, pin them to cores, start services */
void core_os_start(void);

/* Return the state mailbox queue handle so other modules can post events. */
QueueHandle_t core_os_get_state_queue(void);
