#pragma once

#include <stdbool.h>

typedef struct {
    bool safe_mode;
    bool factory_reset;
    bool ota_allowed;
} boot_flags_t;

/* Kernel init: create queues, timers, tasks, but do NOT start scheduler here */
void core_os_init(const boot_flags_t *boot_flags);

/* Kernel start: create tasks, pin them to cores, start services */
void core_os_start(void);
