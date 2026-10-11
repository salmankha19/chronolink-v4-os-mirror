#include "hal.h"
#include "hal_gpio.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "hal_gpio_safe.h"

void HAL_GPIO_Init(void)
{
    /* No board-independent GPIO init is needed: pdl_board_init() has
     * already configured every pin the board actually uses (I2C, SPI,
     * display control, optional LED). This hook exists so HAL_Init()
     * has a stable call site if a future chip or board needs it.
     *
     * An earlier version configured GPIO 2 as an output here as an
     * example. It was dead code -- PDL_PIN_LED_STATUS is -1 on this
     * board, so nothing read or drove GPIO 2 -- and it was removed to
     * avoid confusing readers about whether GPIO 2 had a real role. */
}

void HAL_GPIO_Write(uint32_t pin, gpio_level_t level)
{
    /* Single-pin wrapper. Range-checked inside board_gpio_set_level(). */
    board_gpio_set_level((int)pin, (int)level, "HAL_GPIO_Write");
}

gpio_level_t HAL_GPIO_Read(uint32_t pin)
{
    return gpio_get_level((gpio_num_t)pin) ? GPIO_HIGH : GPIO_LOW;
}