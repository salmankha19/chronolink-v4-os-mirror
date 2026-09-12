/*
 * sensor_api.c
 *
 * Per-quantity sensor API, backed by chronolink_hal's HAL_Sensors_Read(),
 * which aggregates whichever sensor drivers are actually compiled in
 * (see components/chronolink_hal/Kconfig -> Sensors). This file does
 * not talk to any sensor chip directly.
 */
#include "sensor_api.h"
#include "hal_sensors.h"

bool sensor_get_temperature(float *celsius)
{
    if (!celsius) return false;
    *celsius = 0.0f;

    hal_sensor_data_t data;
    if (HAL_Sensors_Read(&data) != HAL_OK) return false;
    if (!(data.valid & HAL_SENSOR_CAP_TEMPERATURE)) return false;

    *celsius = data.temperature_c;
    return true;
}

bool sensor_get_humidity(float *percent_rh)
{
    if (!percent_rh) return false;
    *percent_rh = 0.0f;

    hal_sensor_data_t data;
    if (HAL_Sensors_Read(&data) != HAL_OK) return false;
    if (!(data.valid & HAL_SENSOR_CAP_HUMIDITY)) return false;

    *percent_rh = data.humidity_pct;
    return true;
}

bool sensor_get_pressure(float *hpa)
{
    if (!hpa) return false;
    *hpa = 0.0f;

    hal_sensor_data_t data;
    if (HAL_Sensors_Read(&data) != HAL_OK) return false;
    if (!(data.valid & HAL_SENSOR_CAP_PRESSURE)) return false;

    *hpa = data.pressure_hpa;
    return true;
}

bool sensor_get_light(float *lux)
{
    if (!lux) return false;
    *lux = 0.0f;

    hal_sensor_data_t data;
    if (HAL_Sensors_Read(&data) != HAL_OK) return false;
    if (!(data.valid & HAL_SENSOR_CAP_LIGHT)) return false;

    *lux = data.ambient_light;
    return true;
}
