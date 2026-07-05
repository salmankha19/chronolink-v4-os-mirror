#ifndef CHRONOLINK_HAL_SENSORS_H
#define CHRONOLINK_HAL_SENSORS_H

#include "hal.h"

typedef struct {
    float temperature_c;
    float ambient_light;
} hal_sensor_data_t;

hal_status_t HAL_Sensors_Init(void);
hal_status_t HAL_Sensors_Read(hal_sensor_data_t *out);

#endif