/*
 * spi_api.c
 *
 * Thin adapter over chronolink_hal's HAL_SPI_*. See spi_api.h for the
 * cs-pin/bus_id mismatch this doesn't yet solve.
 */
#include "spi_api.h"
#include "hal_spi.h"
#include "esp_log.h"

static const char *TAG = "SPI_API";

bool spi_init(const spi_bus_t *bus)
{
    if (bus && bus->bus_id != 0) {
        ESP_LOGW(TAG, "spi_init: bus_id=%u requested; chronolink_hal's SPI bus "
                      "is shared across all CS-addressed devices, not per bus_id",
                 bus->bus_id);
    }
    return HAL_SPI_Init() == HAL_OK;
}

bool spi_transfer(uint8_t bus_id, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    /* bus_id is repurposed as cs_pin here -- see header note. */
    return HAL_SPI_Transfer(bus_id, tx, rx, len, 0 /* HAL default speed */) == HAL_OK;
}
