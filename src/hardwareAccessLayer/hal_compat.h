/* Compatibility aliases for hardwareAccessLayer (hal) */
#ifndef HAL_COMPAT_H
#define HAL_COMPAT_H

#include "hardwareAccessLayer/hal_gpio.h"

/* Multi-value sensor reading: primary = main metric, secondary = optional metric */
#include <stdint.h>
#include <math.h>

typedef struct {
    float primary;      /* temperature or main metric */
    float secondary;    /* humidity or secondary metric; set to NAN if unused */
    uint64_t timestamp_ms;
    int32_t raw;
} sensor_reading_t;

/* Example alias: keep old pil_gpio symbols available by forwarding to HAL implementations */
/* static inline void pil_gpio_init(uint32_t pin, pil_gpio_mode_t mode) { hal_gpio_init(pin, mode); } */

#endif /* HAL_COMPAT_H */
