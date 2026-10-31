/*
 * adhan_calculator.h
 *
 * ChronoLink V4 OS -- Adhan tier calculator.
 *
 * INTERNAL to svc_prayer_engine_service (src/ is a PRIV_INCLUDE_DIRS
 * path). Not part of the published API.
 *
 * Owns exactly two things: the svc_prayer_engine_calculate() call for one
 * date, and the per-prayer adhan_offset_min[] application. It does NOT
 * touch the timetable, jamat, rounding, seasons, the cache, the event bus,
 * or any log line -- those stay in the service. This is the split
 * described in docs/prayer-times-model.md section 8.1.
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "svc_config.h"
#include "svc_prayer_engine.h"

typedef struct {
    /* Engine raw output (astronomical + auxiliary, no adhan offset) */
    svc_prayer_times_t raw;
    /* Adhan offsets applied: adhan_sec[i] = raw.<prayer> + cfg->adhan_offset_min[i] */
    uint32_t adhan_sec[5];
} adhan_result_t;

esp_err_t adhan_calculator_run(
    const svc_config_t *cfg,
    uint32_t date_yyyymmdd,
    int16_t tz_offset_minutes,
    adhan_result_t *out
);
