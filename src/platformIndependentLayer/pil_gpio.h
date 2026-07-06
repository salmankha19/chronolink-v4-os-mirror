#ifndef PIL_GPIO_H
#define PIL_GPIO_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PIL_GPIO_MODE_INPUT = 0,
    PIL_GPIO_MODE_OUTPUT,
} pil_gpio_mode_t;

/* PIL GPIO ABI: minimal, MCU-agnostic */
void pil_gpio_init(uint32_t pin, pil_gpio_mode_t mode);
void pil_gpio_write(uint32_t pin, bool level);
bool pil_gpio_read(uint32_t pin);

#endif /* PIL_GPIO_H */
