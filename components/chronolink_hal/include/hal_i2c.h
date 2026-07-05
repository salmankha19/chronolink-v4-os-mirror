#ifndef CHRONOLINK_HAL_I2C_H
#define CHRONOLINK_HAL_I2C_H

#include <stdint.h>
#include "hal.h"

hal_status_t HAL_I2C_Init(void);

hal_status_t HAL_I2C_Read(uint8_t dev_addr,
                          uint8_t reg,
                          uint8_t *buf,
                          uint16_t len);

hal_status_t HAL_I2C_Write(uint8_t dev_addr,
                           uint8_t reg,
                           const uint8_t *data,
                           uint16_t len);

#endif