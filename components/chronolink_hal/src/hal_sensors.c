/*
 * hal_sensors.c
 *
 * ChronoLink V4 OS — Unified Sensor HAL Registry
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#include "hal_sensors.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "HAL_SENSORS";

static struct {
    const hal_sensor_driver_t *drv;
    bool inited;
} s_slots[HAL_SENSOR_MAX_DRIVERS];

/* --------------------------------------------------------------------------
 * Registry
 * -------------------------------------------------------------------------- */

hal_status_t HAL_Sensor_Register(const hal_sensor_driver_t *drv)
{
    if (!drv || !drv->read || !drv->name) return HAL_ERR_DEV;

    for (int i = 0; i < HAL_SENSOR_MAX_DRIVERS; i++) {
        if (!s_slots[i].drv) {
            s_slots[i].drv = drv;
            s_slots[i].inited = false;
            ESP_LOGI(TAG, "Registered sensor '%s' caps=0x%02lx",
                     drv->name, (unsigned long)drv->caps);
            return HAL_OK;
        }
    }
    ESP_LOGW(TAG, "Sensor registry full");
    return HAL_ERR_INIT;
}

/* --------------------------------------------------------------------------
 * Legacy init — now iterates registered drivers
 * -------------------------------------------------------------------------- */

hal_status_t HAL_Sensors_Init(void)
{
    for (int i = 0; i < HAL_SENSOR_MAX_DRIVERS; i++) {
        if (s_slots[i].drv && s_slots[i].drv->init && !s_slots[i].inited) {
            hal_status_t hs = s_slots[i].drv->init();
            if (hs == HAL_OK) {
                s_slots[i].inited = true;
                ESP_LOGI(TAG, "Init OK: %s", s_slots[i].drv->name);
            } else {
                ESP_LOGW(TAG, "Init FAIL: %s (%d)", s_slots[i].drv->name, (int)hs);
            }
        }
    }
    return HAL_OK;
}

/* --------------------------------------------------------------------------
 * Legacy read — aggregates all active sensors into one struct
 * -------------------------------------------------------------------------- */

hal_status_t HAL_Sensors_Read(hal_sensor_data_t *out)
{
    if (!out) return HAL_ERR_DEV;

    memset(out, 0, sizeof(*out));

    bool any_ok = false;
    for (int i = 0; i < HAL_SENSOR_MAX_DRIVERS; i++) {
        if (s_slots[i].inited) {
            hal_sensor_data_t tmp = {0};
            if (s_slots[i].drv->read(&tmp) == HAL_OK) {
                if (tmp.valid & HAL_SENSOR_CAP_TEMPERATURE) out->temperature_c = tmp.temperature_c;
                if (tmp.valid & HAL_SENSOR_CAP_HUMIDITY)    out->humidity_pct   = tmp.humidity_pct;
                if (tmp.valid & HAL_SENSOR_CAP_PRESSURE)    out->pressure_hpa   = tmp.pressure_hpa;
                if (tmp.valid & HAL_SENSOR_CAP_LIGHT)       out->ambient_light  = tmp.ambient_light;
                out->valid |= tmp.valid;
                any_ok = true;
            }
        }
    }
    return any_ok ? HAL_OK : HAL_ERR_DEV;
}

/* --------------------------------------------------------------------------
 * New API
 * -------------------------------------------------------------------------- */

hal_status_t HAL_Sensor_ReadByName(const char *name, hal_sensor_data_t *out)
{
    if (!name || !out) return HAL_ERR_DEV;

    for (int i = 0; i < HAL_SENSOR_MAX_DRIVERS; i++) {
        if (s_slots[i].drv && strcmp(s_slots[i].drv->name, name) == 0) {
            if (!s_slots[i].inited) return HAL_ERR_INIT;
            return s_slots[i].drv->read(out);
        }
    }
    return HAL_ERR_DEV;
}

void HAL_Sensor_ReadAll(hal_sensor_data_t *out, uint8_t *count)
{
    if (!out || !count) return;

    uint8_t n = 0;
    for (int i = 0; i < HAL_SENSOR_MAX_DRIVERS && n < HAL_SENSOR_MAX_DRIVERS; i++) {
        if (s_slots[i].inited) {
            hal_sensor_data_t tmp = {0};
            if (s_slots[i].drv->read(&tmp) == HAL_OK) {
                out[n++] = tmp;
            }
        }
    }
    *count = n;
}