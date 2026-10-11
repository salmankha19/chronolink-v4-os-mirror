#ifndef CHRONOLINK_HAL_GPIO_SAFE_H
#define CHRONOLINK_HAL_GPIO_SAFE_H

#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Range-checked GPIO helpers. Every caller in the HAL and PDL goes
 * through these so that a bad pin number produces a warning log
 * instead of an ESP-IDF assertion or a silent write to the wrong pin.
 *
 * Both functions accept a single pin index (0..CONFIG_SOC_GPIO_PIN_COUNT-1).
 *
 * There is deliberately no "mask or index" variant. An earlier version
 * exposed board_gpio_set_level_maskaware() / board_gpio_set_direction_
 * maskaware(), which tried to accept either form in one uint64_t. That
 * cannot be made unambiguous -- (1 << 5) and pin 32 are the same
 * integer -- and it caused a real bug: a mask argument was interpreted
 * as a pin index. Callers that need to set several pins should call
 * gpio_set_level() in a loop, or add a properly-typed mask function
 * with a distinct name and a mask-only signature. */
void board_gpio_set_level(int pin, int level, const char *who);
void board_gpio_set_direction(int pin, gpio_mode_t mode, const char *who);

#ifdef __cplusplus
}
#endif

#endif