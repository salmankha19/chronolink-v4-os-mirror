#ifndef CHRONOLINK_HAL_GPIO_H
#define CHRONOLINK_HAL_GPIO_H

#include <stdint.h>
#include "hal.h"

typedef enum {
    GPIO_LOW = 0,
    GPIO_HIGH = 1
} gpio_level_t;

void HAL_GPIO_Init(void);
void HAL_GPIO_Write(uint32_t pin, gpio_level_t level);
gpio_level_t HAL_GPIO_Read(uint32_t pin);

#endif
