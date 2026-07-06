/* Compatibility aliases for hardwareAccessLayer (hal) */
#ifndef HAL_COMPAT_H
#define HAL_COMPAT_H

#include "hardwareAccessLayer/hal_gpio.h"

/* Example alias: keep old pil_gpio symbols available by forwarding to HAL implementations */
/* static inline void pil_gpio_init(uint32_t pin, pil_gpio_mode_t mode) { hal_gpio_init(pin, mode); } */

#endif /* HAL_COMPAT_H */
