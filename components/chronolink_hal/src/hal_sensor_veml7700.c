#include "hal_sensor_veml7700.h"
#include "hal_i2c.h"
#include "esp_log.h"

#define VEML7700_ADDR     0x10
#define VEML7700_CONFIG   0x00
#define VEML7700_ALS_DATA 0x04

static const char *TAG = "VEML7700";

static hal_status_t veml7700_init(void)
{
    /* Power on, default config 0x0000 */
    uint8_t tx[3] = { VEML7700_CONFIG, 0x00, 0x00 };
    hal_status_t hs = HAL_I2C_Transmit(VEML7700_ADDR, tx, sizeof(tx));
    if (hs == HAL_OK) {
        ESP_LOGI(TAG, "VEML7700 init done");
    } else {
        ESP_LOGW(TAG, "VEML7700 init failed");
    }
    return hs;
}

static hal_status_t veml7700_read(hal_sensor_data_t *out)
{
    if (!out) return HAL_ERR_DEV;

    uint8_t cmd = VEML7700_ALS_DATA;
    uint8_t rx[2];

    if (HAL_I2C_TransmitReceive(VEML7700_ADDR, &cmd, 1, rx, 2) != HAL_OK) {
        return HAL_ERR_BUS;
    }

    uint16_t raw = ((uint16_t)rx[1] << 8) | rx[0];
    out->ambient_light = raw * 0.0036f;
    out->valid = HAL_SENSOR_CAP_LIGHT;
    return HAL_OK;
}

const hal_sensor_driver_t HAL_SENSOR_VEML7700_DRIVER = {
    .name   = "veml7700",
    .caps   = HAL_SENSOR_CAP_LIGHT,
    .init   = veml7700_init,
    .deinit = NULL,
    .read   = veml7700_read,
};