#include "hal_gpio_safe.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "soc/soc_caps.h"
#include <stdint.h>

static const char *TAG_HAL_GPIO = "HAL_GPIO";

/* Number of GPIOs on the target. Comes from soc_caps.h, so this
 * tracks the actual chip rather than a hardcoded guess. Was 48 before,
 * which silently rejected GPIO 48 (PDL_PIN_I2S_WS on this board). */
#ifndef CONFIG_SOC_GPIO_PIN_COUNT
#error "CONFIG_SOC_GPIO_PIN_COUNT not defined -- soc/soc_caps.h should provide it"
#endif

static inline bool pin_is_valid(int pin)
{
    return (pin >= 0 && pin < CONFIG_SOC_GPIO_PIN_COUNT);
}

void board_gpio_set_level(int pin, int level, const char *who)
{
    if (pin_is_valid(pin)) {
        ESP_LOGD(TAG_HAL_GPIO, "%s: gpio_set_level pin=%d level=%d",
                 who ? who : "board_gpio_set_level", pin, level);
        gpio_set_level((gpio_num_t)pin, (uint32_t)level);
    } else {
        ESP_LOGW(TAG_HAL_GPIO, "%s: invalid pin %d, skipping gpio_set_level",
                 who ? who : "board_gpio_set_level", pin);
    }
}

void board_gpio_set_direction(int pin, gpio_mode_t mode, const char *who)
{
    if (pin_is_valid(pin)) {
        gpio_set_direction((gpio_num_t)pin, mode);
    } else {
        ESP_LOGW(TAG_HAL_GPIO, "%s: invalid pin %d, skipping gpio_set_direction",
                 who ? who : "board_gpio_set_direction", pin);
    }
}