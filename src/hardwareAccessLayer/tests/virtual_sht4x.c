/*
 * virtual_sht4x.c
 *
 * Virtual SHT4x temperature + humidity sensor driver for test harness.
 * Exports VIRTUAL_SHT4X_DRIVER.
 */

#include "hal_driver_ext.h"
#include "hal_compat.h"
#include "hal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static hal_config_t _cfg;
static int _inited = 0;

/* init/deinit */
static hal_status_t vsht_init(const hal_config_t *cfg) {
    if (!cfg) return HAL_ERR_INIT;
    _cfg = *cfg;
    _inited = 1;
    return HAL_OK;
}

static hal_status_t vsht_deinit(void) {
    _inited = 0;
    return HAL_OK;
}

/* read: fills sensor_reading_t with temperature in Celsius by default.
   For tests we return temperature in primary; callers can interpret value depending on role. */
static hal_status_t vsht_read(void *out) {
    if (!out) return HAL_ERR_INIT;
    if (!_inited) return HAL_ERR_INIT;
    sensor_reading_t *r = (sensor_reading_t*)out;

    /* deterministic fake values for tests */
    r->primary = 23.75f;               /* temperature Celsius by default */
    r->secondary = NAN;                /* humidity not provided by default */
    r->raw = 2375;                     /* scaled raw */
    r->timestamp_ms = (uint64_t)time(NULL) * 1000ULL;
    return HAL_OK;
}

/* health */
static hal_status_t vsht_get_health(sensor_health_t *out) {
    if (!out) return HAL_ERR_INIT;
    *out = _inited ? SENSOR_HEALTH_OK : SENSOR_HEALTH_FAIL;
    return HAL_OK;
}

/* capabilities: SHT4x supports temperature and humidity */
static hal_status_t vsht_get_caps(device_caps_t *out) {
    if (!out) return HAL_ERR_INIT;
    memset(out, 0, sizeof(*out));
    out->type = DEV_TYPE_SENSOR;
    out->caps.sensor.temperature = true;
    out->caps.sensor.humidity = true;
    out->caps.sensor.pressure = false;
    out->caps.sensor.lux = false;
    out->caps.sensor.co2 = false;
    out->caps.sensor.motion = false;
    return HAL_OK;
}

/* ext API: allow switching measurement mode (single/periodic) or heater control */
static hal_status_t vsht_set_measurement_mode(int mode) {
    (void)mode;
    /* no-op for virtual driver */
    return HAL_OK;
}
static hal_status_t vsht_set_heater(int on) {
    (void)on;
    /* no-op for virtual driver */
    return HAL_OK;
}

static sensor_ext_api_t _sht_ext = {
    .set_oversampling = NULL,        /* not used for SHT4x */
    .start_continuous = NULL         /* not used; we expose custom below if needed */
};

/* pack custom functions into raw ext pointer if needed */
static hal_ext_api_u _ext_api_union = { .sensor = &_sht_ext };

const hal_driver_t VIRTUAL_SHT4X_DRIVER = {
    .driver_name = "virtual_sht4x",
    .driver_version = "0.1",
    .device_type = DEV_TYPE_SENSOR,
    .init = vsht_init,
    .deinit = vsht_deinit,
    .read = vsht_read,
    .write = NULL,
    .get_health = vsht_get_health,
    .get_capabilities = vsht_get_caps,
    .ext_api = _ext_api_union
};