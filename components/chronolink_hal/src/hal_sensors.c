#include "hal_sensors.h"

hal_status_t HAL_Sensors_Init(void)
{
    return HAL_OK;
}

hal_status_t HAL_Sensors_Read(hal_sensor_data_t *out)
{
    out->temperature_c = 25.0f;
    out->ambient_light = 100.0f;
    return HAL_OK;
}
