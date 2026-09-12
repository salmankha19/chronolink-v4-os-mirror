/*
 * i2c_api.c
 *
 * Thin adapter over chronolink_hal's HAL_I2C_*. See i2c_api.h for the
 * single-bus limitation.
 */
#include "i2c_api.h"
#include "hal_i2c.h"
#include "esp_log.h"

static const char *TAG = "I2C_API";

bool i2c_init(const i2c_bus_t *bus)
{
    if (bus && bus->bus_id != 0) {
        ESP_LOGW(TAG, "i2c_init: bus_id=%u requested but chronolink_hal only "
                      "exposes a single I2C bus -- ignoring bus_id/freq_hz",
                 bus->bus_id);
    }
    return HAL_I2C_Init() == HAL_OK;
}

bool i2c_write(uint8_t bus_id, uint8_t addr, const uint8_t *data, uint16_t len)
{
    if (bus_id != 0) {
        ESP_LOGW(TAG, "i2c_write: bus_id=%u not supported, using the only bus", bus_id);
    }
    return HAL_I2C_Transmit(addr, data, len) == HAL_OK;
}

bool i2c_read(uint8_t bus_id, uint8_t addr, uint8_t *data, uint16_t len)
{
    if (bus_id != 0) {
        ESP_LOGW(TAG, "i2c_read: bus_id=%u not supported, using the only bus", bus_id);
    }
    return HAL_I2C_Receive(addr, data, len) == HAL_OK;
}
