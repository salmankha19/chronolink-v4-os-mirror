/*
 * hal_sensors.h
 *
 * ChronoLink V4 OS — Unified Sensor HAL
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#ifndef CHRONOLINK_HAL_SENSORS_H
#define CHRONOLINK_HAL_SENSORS_H

#include "hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_SENSOR_MAX_DRIVERS 4

typedef enum {
    HAL_SENSOR_CAP_TEMPERATURE = (1u << 0),
    HAL_SENSOR_CAP_HUMIDITY    = (1u << 1),
    HAL_SENSOR_CAP_PRESSURE    = (1u << 2),
    HAL_SENSOR_CAP_LIGHT       = (1u << 3),
} hal_sensor_cap_t;

typedef struct {
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;
    float ambient_light;   /* renamed from light_lux for backward compat */
    uint32_t valid;        /* bitmask of hal_sensor_cap_t */
} hal_sensor_data_t;

typedef struct {
    const char *name;
    uint32_t caps;
    hal_status_t (*init)(void);
    hal_status_t (*deinit)(void);
    hal_status_t (*read)(hal_sensor_data_t *out);
} hal_sensor_driver_t;

/* Legacy API — now backed by the registry */
hal_status_t HAL_Sensors_Init(void);
hal_status_t HAL_Sensors_Read(hal_sensor_data_t *out);

/* New registry API */
hal_status_t HAL_Sensor_Register(const hal_sensor_driver_t *drv);
hal_status_t HAL_Sensor_ReadByName(const char *name, hal_sensor_data_t *out);
void HAL_Sensor_ReadAll(hal_sensor_data_t *out, uint8_t *count);

#ifdef __cplusplus
}
#endif

#endif