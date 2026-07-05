#ifndef CHRONOLINK_HAL_SPI_H
#define CHRONOLINK_HAL_SPI_H

#include <stdint.h>
#include "hal.h"

hal_status_t HAL_SPI_Init(void);

hal_status_t HAL_SPI_Transfer(uint8_t dev_id,
                              const uint8_t *tx,
                              uint8_t *rx,
                              uint16_t len);

#endif