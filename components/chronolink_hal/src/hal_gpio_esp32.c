#include "hal.h"
#include "hal_gpio.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "hal_gpio_safe.h"

void HAL_GPIO_Init(void)
{
    // Example: configure GPIO 2 as output
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << 2),
        .mode = GPIO_MODE_OUTPUT
    };
    gpio_config(&cfg);
}

void HAL_GPIO_Write(uint32_t pin, gpio_level_t level)
{
    board_gpio_set_level_maskaware(pin, level, "HAL_GPIO_Write");
}

gpio_level_t HAL_GPIO_Read(uint32_t pin)
{
    return gpio_get_level(pin) ? GPIO_HIGH : GPIO_LOW;
}
