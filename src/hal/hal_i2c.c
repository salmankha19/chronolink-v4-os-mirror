/* HAL I2C driver placeholder. Implement MCU-specific I2C here. */

#include "pil_i2c.h"

bool pil_i2c_init(const pil_i2c_bus_t *bus) { (void)bus; return true; }
bool pil_i2c_write(uint8_t bus_id, uint8_t addr, const uint8_t *data, uint16_t len)
{ (void)bus_id; (void)addr; (void)data; (void)len; return false; }
bool pil_i2c_read(uint8_t bus_id, uint8_t addr, uint8_t *data, uint16_t len)
{ (void)bus_id; (void)addr; (void)data; (void)len; return false; }
