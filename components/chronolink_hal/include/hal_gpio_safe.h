#ifndef CHRONOLINK_HAL_GPIO_SAFE_H
#define CHRONOLINK_HAL_GPIO_SAFE_H

#if defined(__has_include)
#  if __has_include("driver/gpio.h")
#    include "driver/gpio.h"
#  elif __has_include("esp_driver_gpio/include/driver/gpio.h")
#    include "esp_driver_gpio/include/driver/gpio.h"
#  else
#    error "No compatible GPIO header found for ChronoLink HAL"
#  endif
#else
#  include "driver/gpio.h"
#endif

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
