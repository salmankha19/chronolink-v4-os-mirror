#ifndef HIL_GPIO_API_H
#define HIL_GPIO_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Named hil_gpio_mode_t (not gpio_mode_t) deliberately: ESP-IDF's own
   driver/gpio.h already defines a `gpio_mode_t` enum with enumerators
   literally named GPIO_MODE_INPUT/GPIO_MODE_OUTPUT. gpio_api.c pulls
   that header in transitively via hal_gpio_safe.h, so reusing those
   exact names here would be a redeclaration, not an override. */
typedef enum {
    HIL_GPIO_MODE_INPUT = 0,
    HIL_GPIO_MODE_OUTPUT,
} hil_gpio_mode_t;

/* Low-level, MCU-agnostic GPIO ABI. Internally a thin adapter over
   chronolink_hal's board_gpio_set_direction() / HAL_GPIO_Write/Read --
   see gpio_api.c. Mainly used by boot_api.c (button reads) and any
   service that needs a raw digital pin rather than a named sensor/
   display/audio role. */
void gpio_init(uint32_t pin, hil_gpio_mode_t mode);
void gpio_write(uint32_t pin, bool level);
bool gpio_read(uint32_t pin);

#ifdef __cplusplus
}
#endif

#endif /* HIL_GPIO_API_H */
