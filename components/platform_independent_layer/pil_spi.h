#ifndef PIL_SPI_H
#define PIL_SPI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t bus_id;
    uint32_t freq_hz;
} pil_spi_bus_t;

/* PIL SPI ABI */
bool pil_spi_init(const pil_spi_bus_t *bus);
bool pil_spi_transfer(uint8_t bus_id, const uint8_t *tx, uint8_t *rx, uint16_t len);

#endif /* PIL_SPI_H */
