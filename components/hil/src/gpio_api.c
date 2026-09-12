/*
 * gpio_api.c
 *
 * Thin adapter, not a driver: all real ESP32 register access lives in
 * components/chronolink_hal (hal_gpio_esp32.c / hal_gpio_safe.c). This
 * just translates the MCU-agnostic gpio_* calls into the already-tested
 * chronolink_hal calls.
 */
#include "gpio_api.h"
#include "hal_gpio.h"
#include "hal_gpio_safe.h"

void gpio_init(uint32_t pin, hil_gpio_mode_t mode)
{
    /* board_gpio_set_direction() validates the pin index and logs a
       warning instead of crashing on an out-of-range pin. */
    board_gpio_set_direction((int)pin,
        (mode == HIL_GPIO_MODE_OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
        "gpio_init");
}

void gpio_write(uint32_t pin, bool level)
{
    HAL_GPIO_Write(pin, level ? GPIO_HIGH : GPIO_LOW);
}

bool gpio_read(uint32_t pin)
{
    return HAL_GPIO_Read(pin) == GPIO_HIGH;
}
