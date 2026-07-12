/* HAL SPI driver placeholder. Implement MCU-specific SPI here. */

#include "pil_spi.h"

bool pil_spi_init(const pil_spi_bus_t *bus) { (void)bus; return true; }
bool pil_spi_transfer(uint8_t bus_id, const uint8_t *tx, uint8_t *rx, uint16_t len)
{ (void)bus_id; (void)tx; (void)rx; (void)len; return false; }
