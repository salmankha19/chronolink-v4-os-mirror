#pragma once

#include "boot_events.h"

/* Kernel init: create queues, timers, tasks, but do NOT start scheduler here */
void core_os_init(boot_flags_t boot_flags);

/* Kernel start: create tasks, pin them to cores, start services */
void core_os_start(void);
