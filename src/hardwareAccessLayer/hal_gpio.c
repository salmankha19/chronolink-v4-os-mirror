/* HAL GPIO driver placeholder.
   Implement MCU-specific GPIO operations here and call pil_gpio_* ABI.
*/

#include "pil_gpio.h"

/* Empty stubs to satisfy linker while HAL is implemented. */
void pil_gpio_init(uint32_t pin, pil_gpio_mode_t mode) { (void)pin; (void)mode; }
void pil_gpio_write(uint32_t pin, bool level) { (void)pin; (void)level; }
bool pil_gpio_read(uint32_t pin) { (void)pin; return false; }
