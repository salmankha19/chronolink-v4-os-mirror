#ifndef CHRONOLINK_HAL_GPIO_SAFE_H
#define CHRONOLINK_HAL_GPIO_SAFE_H

#include <stdint.h>
#include "driver/gpio.h"

void board_gpio_set_level(int pin, int level, const char *who);
void board_gpio_set_level_maskaware(uint64_t pin_or_mask, int level, const char *who);
void board_gpio_set_direction(int pin, gpio_mode_t mode, const char *who);
void board_gpio_set_direction_maskaware(uint64_t pin_or_mask, gpio_mode_t mode, const char *who);

#endif
