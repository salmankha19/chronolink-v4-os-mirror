#include "hal_sensor_bme280.h"
#include "hal_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define BME280_ADDR        0x76
#define BME280_CHIP_ID     0x60
#define BME280_REG_ID      0xD0
#define BME280_REG_CALIB0  0x88
#define BME280_REG_CALIB1  0xE1
#define BME280_REG_CTRL_HUM  0xF2
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_DATA    0xF7

static const char *TAG = "BME280";

typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;
} bme280_calib_t;

static bme280_calib_t s_calib;
static double s_t_fine;
static bool s_inited = false;

static hal_status_t bme280_read_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    return HAL_I2C_Read(BME280_ADDR, reg, buf, len);
}

static hal_status_t bme280_write_reg(uint8_t reg, uint8_t val)
{
    return HAL_I2C_Write(BME280_ADDR, reg, &val, 1);
}

static hal_status_t bme280_read_calib(void)
{
    uint8_t buf[26], buf1[7];
    hal_status_t hs = bme280_read_reg(BME280_REG_CALIB0, buf, 26);
    if (hs != HAL_OK) return hs;
    hs = bme280_read_reg(BME280_REG_CALIB1, buf1, 7);
    if (hs != HAL_OK) return hs;

    s_calib.dig_T1 = (buf[1] << 8) | buf[0];
    s_calib.dig_T2 = (int16_t)((buf[3] << 8) | buf[2]);
    s_calib.dig_T3 = (int16_t)((buf[5] << 8) | buf[4]);
    s_calib.dig_P1 = (buf[7] << 8) | buf[6];
    s_calib.dig_P2 = (int16_t)((buf[9] << 8) | buf[8]);
    s_calib.dig_P3 = (int16_t)((buf[11] << 8) | buf[10]);
    s_calib.dig_P4 = (int16_t)((buf[13] << 8) | buf[12]);
    s_calib.dig_P5 = (int16_t)((buf[15] << 8) | buf[14]);
    s_calib.dig_P6 = (int16_t)((buf[17] << 8) | buf[16]);
    s_calib.dig_P7 = (int16_t)((buf[19] << 8) | buf[18]);
    s_calib.dig_P8 = (int16_t)((buf[21] << 8) | buf[20]);
    s_calib.dig_P9 = (int16_t)((buf[23] << 8) | buf[22]);
    s_calib.dig_H1 = buf[25];

    s_calib.dig_H2 = (int16_t)((buf1[1] << 8) | buf1[0]);
    s_calib.dig_H3 = buf1[2];
    s_calib.dig_H4 = (int16_t)(((int8_t)buf1[3] << 4) | (buf1[4] & 0x0F));
    s_calib.dig_H5 = (int16_t)(((int8_t)buf1[5] << 4) | (buf1[4] >> 4));
    s_calib.dig_H6 = (int8_t)buf1[6];
    return HAL_OK;
}

static double bme280_compensate_temp(int32_t adc_T)
{
    double v1 = (((double)adc_T)/16384.0 - ((double)s_calib.dig_T1)/1024.0) * ((double)s_calib.dig_T2);
    double v2 = ((((double)adc_T)/131072.0 - ((double)s_calib.dig_T1)/8192.0) *
                 (((double)adc_T)/131072.0 - ((double)s_calib.dig_T1)/8192.0)) * ((double)s_calib.dig_T3);
    s_t_fine = v1 + v2;
    return s_t_fine / 5120.0;
}

static double bme280_compensate_press(int32_t adc_P)
{
    double v1 = (s_t_fine / 2.0) - 64000.0;
    double v2 = v1 * v1 * ((double)s_calib.dig_P6) / 32768.0;
    v2 = v2 + v1 * ((double)s_calib.dig_P5) * 2.0;
    v2 = (v2 / 4.0) + (((double)s_calib.dig_P4) * 65536.0);
    v1 = (((double)s_calib.dig_P3) * v1 * v1 / 524288.0 + ((double)s_calib.dig_P2) * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * ((double)s_calib.dig_P1);
    if (v1 == 0.0) return 0.0;
    double p = 1048576.0 - (double)adc_P;
    p = (p - (v2 / 4096.0)) * 6250.0 / v1;
    v1 = ((double)s_calib.dig_P9) * p * p / 2147483648.0;
    v2 = p * ((double)s_calib.dig_P8) / 32768.0;
    p = p + (v1 + v2 + ((double)s_calib.dig_P7)) / 16.0;
    return p;
}

static double bme280_compensate_hum(int32_t adc_H)
{
    double var_H = (((double)s_t_fine) - 76800.0);
    var_H = (((double)adc_H) - (((double)s_calib.dig_H4) * 64.0 + ((double)s_calib.dig_H5) / 16384.0 * var_H)) *
            (((double)s_calib.dig_H2) / 65536.0 * (1.0 + ((double)s_calib.dig_H6) / 67108864.0 * var_H *
            (1.0 + ((double)s_calib.dig_H3) / 67108864.0 * var_H)));
    var_H = var_H * (1.0 - ((double)s_calib.dig_H1) * var_H / 524288.0);
    if (var_H > 100.0) var_H = 100.0;
    else if (var_H < 0.0) var_H = 0.0;
    return var_H;
}

static hal_status_t bme280_init(void)
{
    uint8_t id = 0;
    if (bme280_read_reg(BME280_REG_ID, &id, 1) != HAL_OK || id != BME280_CHIP_ID) {
        ESP_LOGW(TAG, "BME280 not detected (id=0x%02x)", id);
        return HAL_ERR_DEV;
    }
    if (bme280_read_calib() != HAL_OK) {
        ESP_LOGE(TAG, "BME280 calib read failed");
        return HAL_ERR_DEV;
    }
    bme280_write_reg(BME280_REG_CTRL_HUM, 0x01);   /* hum os x1 */
    bme280_write_reg(BME280_REG_CTRL_MEAS, 0x23);  /* temp/press x1, forced mode */

    s_inited = true;
    ESP_LOGI(TAG, "BME280 initialized");
    return HAL_OK;
}

static hal_status_t bme280_read(hal_sensor_data_t *out)
{
    if (!out) return HAL_ERR_DEV;
    if (!s_inited) return HAL_ERR_INIT;

    /* Trigger forced measurement */
    bme280_write_reg(BME280_REG_CTRL_MEAS, 0x23);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t d[8];
    if (bme280_read_reg(BME280_REG_DATA, d, 8) != HAL_OK) return HAL_ERR_BUS;

    int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    int32_t adc_H = ((int32_t)d[6] << 8) | d[7];

    out->temperature_c = (float)bme280_compensate_temp(adc_T);
    out->pressure_hpa  = (float)(bme280_compensate_press(adc_P) / 100.0);
    out->humidity_pct  = (float)bme280_compensate_hum(adc_H);
    out->valid = HAL_SENSOR_CAP_TEMPERATURE | HAL_SENSOR_CAP_HUMIDITY | HAL_SENSOR_CAP_PRESSURE;
    return HAL_OK;
}

const hal_sensor_driver_t HAL_SENSOR_BME280_DRIVER = {
    .name   = "bme280",
    .caps   = HAL_SENSOR_CAP_TEMPERATURE | HAL_SENSOR_CAP_HUMIDITY | HAL_SENSOR_CAP_PRESSURE,
    .init   = bme280_init,
    .deinit = NULL,
    .read   = bme280_read,
};