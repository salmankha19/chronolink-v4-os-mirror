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
                           
/* Raw I2C: transmit a buffer without a register prefix */
hal_status_t HAL_I2C_Transmit(uint8_t dev_addr,
                              const uint8_t *data,
                              uint16_t len);

/* Raw I2C: read into buffer without first writing a register */
hal_status_t HAL_I2C_Receive(uint8_t dev_addr,
                             uint8_t *buf,
                             uint16_t len);

/* Raw I2C: write then read in one transaction */
hal_status_t HAL_I2C_TransmitReceive(uint8_t dev_addr,
                                      const uint8_t *tx_data,
                                      uint16_t tx_len,
                                      uint8_t *rx_buf,
                                      uint16_t rx_len);

#endif
