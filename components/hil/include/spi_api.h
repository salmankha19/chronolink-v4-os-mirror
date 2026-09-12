#ifndef HIL_SPI_API_H
#define HIL_SPI_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t bus_id;
    uint32_t freq_hz;
} spi_bus_t;

/* Low-level SPI ABI. KNOWN LIMITATION (see spi_api.c): chronolink_hal's
   SPI is chip-select-addressed with a per-transfer clock speed; this
   ABI has neither. bus_id is repurposed as the CS pin until this header
   grows a real cs_pin field -- fine for one SPI device, not enough for
   multiple SPI devices sharing the bus. */
bool spi_init(const spi_bus_t *bus);
bool spi_transfer(uint8_t bus_id, const uint8_t *tx, uint8_t *rx, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SPI_API_H */
