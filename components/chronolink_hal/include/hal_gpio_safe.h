#ifndef CHRONOLINK_HAL_GPIO_SAFE_H
#define CHRONOLINK_HAL_GPIO_SAFE_H

#include "driver/gpio.h"

/* Configure one or more GPIO pins as push-pull outputs.
   Returns the underlying ESP-IDF status from gpio_config(). */

static inline esp_err_t hal_gpio_config_outputs(uint64_t pin_mask)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cfg);
}

#endif
