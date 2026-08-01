#include "hal_sensor_sht4x.h"
#include "hal_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHT4X_ADDR         0x44
#define SHT4X_CMD_MEAS_HI  0xFD
#define SHT4X_CMD_RESET    0x94

static const char *TAG = "SHT4X";
static bool s_inited = false;

static hal_status_t sht4x_init(void)
{
    uint8_t cmd = SHT4X_CMD_RESET;
    if (HAL_I2C_Transmit(SHT4X_ADDR, &cmd, 1) != HAL_OK) {
        ESP_LOGW(TAG, "SHT4x not responding");
        return HAL_ERR_DEV;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Probe with a measurement */
    cmd = SHT4X_CMD_MEAS_HI;
    if (HAL_I2C_Transmit(SHT4X_ADDR, &cmd, 1) != HAL_OK) return HAL_ERR_DEV;
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t rx[6];
    if (HAL_I2C_Receive(SHT4X_ADDR, rx, 6) != HAL_OK) {
        ESP_LOGW(TAG, "SHT4x probe read failed");
        return HAL_ERR_DEV;
    }

    s_inited = true;
    ESP_LOGI(TAG, "SHT4x initialized");
    return HAL_OK;
}

static hal_status_t sht4x_read(hal_sensor_data_t *out)
{
    if (!out) return HAL_ERR_DEV;
    if (!s_inited) return HAL_ERR_INIT;

    uint8_t cmd = SHT4X_CMD_MEAS_HI;
    if (HAL_I2C_Transmit(SHT4X_ADDR, &cmd, 1) != HAL_OK) return HAL_ERR_BUS;
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t rx[6];
    if (HAL_I2C_Receive(SHT4X_ADDR, rx, 6) != HAL_OK) return HAL_ERR_BUS;

    uint16_t raw_t = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t raw_h = ((uint16_t)rx[3] << 8) | rx[4];

    out->temperature_c = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    out->humidity_pct  = -6.0f  + 125.0f * ((float)raw_h / 65535.0f);
    if (out->humidity_pct < 0.0f)   out->humidity_pct = 0.0f;
    if (out->humidity_pct > 100.0f) out->humidity_pct = 100.0f;

    out->valid = HAL_SENSOR_CAP_TEMPERATURE | HAL_SENSOR_CAP_HUMIDITY;
    return HAL_OK;
}

const hal_sensor_driver_t HAL_SENSOR_SHT4X_DRIVER = {
    .name   = "sht4x",
    .caps   = HAL_SENSOR_CAP_TEMPERATURE | HAL_SENSOR_CAP_HUMIDITY,
    .init   = sht4x_init,
    .deinit = NULL,
    .read   = sht4x_read,
};