#ifndef CHRONOLINK_HAL_SPI_H
#define CHRONOLINK_HAL_SPI_H

#include <stdint.h>
#include <stddef.h>
#include "hal.h"

/* Initialize the shared SPI bus.

   HAL_SPI_Init() is the single owner of the bus configuration for
   HAL_SPI_HOST. It must be called once during HAL_Init(), before any
   driver that uses the bus (e.g. the ST7796S display backend).

   Idempotent: repeated calls are no-ops. If a bus already exists
   (ESP_ERR_INVALID_STATE from spi_bus_initialize), this returns HAL_OK
   and logs a warning — but note that HAL_SPI_Init is expected to be
   the first caller in normal boot, so that path indicates an init-order
   problem worth investigating. */
hal_status_t HAL_SPI_Init(void);

/* Transfer bytes over SPI to the device selected by cs_pin.
   - cs_pin: GPIO used as chip-select for the target peripheral.
   - tx:     Transmit buffer. May be NULL for read-only transfers.
   - rx:     Receive buffer.  May be NULL for write-only transfers.
   - len:    Number of bytes to transfer (must be > 0).
   - speed_hz: SPI clock in Hz. Pass 0 to use the default.
   Returns HAL_OK on success, HAL_ERR_INIT if the bus/device could not
   be created, HAL_ERR_DEV on transfer failure. */
hal_status_t HAL_SPI_Transfer(uint8_t cs_pin,
                              const uint8_t *tx,
                              uint8_t       *rx,
                              size_t         len,
                              uint32_t       speed_hz);

/* Return the max_transfer_sz value the shared SPI bus was created with.

   Consumers that submit large single transactions (e.g. the ST7796S
   display driver's 8 KB clear/fill chunks) must verify this is at
   least as large as their own chunk size before proceeding. A mismatch
   does not fail at init time — it fails at transfer time with
   ESP_ERR_INVALID_ARG, producing a confusing "txdata transfer > host
   maximum" error deep in the SPI master. Validate here instead. */
int HAL_SPI_GetMaxTransferSize(void);

/* Log all registered SPI devices. Safe to call at any time. */
void HAL_SPI_DebugDump(void);

#endif /* CHRONOLINK_HAL_SPI_H */