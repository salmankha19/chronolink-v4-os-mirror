#ifndef CHRONOLINK_HAL_SPI_H
#define CHRONOLINK_HAL_SPI_H

#include <stdint.h>
#include <stddef.h>
#include "hal.h"

/* Initialize SPI subsystem. Call once during HAL_Init.
   Idempotent; tolerates ESP_ERR_INVALID_STATE if the bus is already
   initialized by another driver (e.g. ST7796S display). */
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

/* Log all registered SPI devices. Safe to call at any time. */
void HAL_SPI_DebugDump(void);

#endif /* CHRONOLINK_HAL_SPI_H */