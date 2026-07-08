#ifndef CHRONOLINK_HAL_SPI_H
#define CHRONOLINK_HAL_SPI_H

#include <stdint.h>
#include <stddef.h>
#include "hal.h"

/* Optional init if you want to pre-initialize the SPI bus.
   If not implemented, HAL_SPI_Transfer will initialize the bus on demand. */
hal_status_t HAL_SPI_Init(void);

/* Simple SPI transfer wrapper for drivers.
   - cs_pin: use PDL_PIN_SPI_CS_* or any GPIO used as CS for the peripheral.
   - tx may be NULL for read-only transfers; rx may be NULL for write-only transfers.
   - len is the number of bytes to transfer.
   - speed_hz is the SPI clock in Hz.
   Returns HAL_OK on success, HAL_ERR_INIT or HAL_ERR_DEV on failure. */
hal_status_t HAL_SPI_Transfer(uint8_t cs_pin,
                             const uint8_t *tx,
                             uint8_t *rx,
                             size_t len,
                             uint32_t speed_hz);

#endif /* CHRONOLINK_HAL_SPI_H */
