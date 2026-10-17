/*
 * svc_prayer_engine.h
 *
 * ChronoLink V4 OS -- Prayer time calculation engine (C wrapper API)
 *
 * ESP32-facing API. The actual math lives in the standalone C++ library
 * at include/prayertimes/ (pure, no ESP-IDF dependency).
 *
 * Callers pass primitive inputs and receive the result in seconds since
 * local midnight. POSIX TZ / DST conversion is the caller's job -- this
 * API takes a fixed timezone_offset in hours.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Calculation methods. Values 0..4 match svc_config_method_t exactly.
   Value 5 (Jafaria) is unique to the prayer engine and has no
   svc_config equivalent today. */
typedef enum {
    SVC_PRAYER_METHOD_MWL         = 0,
    SVC_PRAYER_METHOD_ISNA        = 1,
    SVC_PRAYER_METHOD_EGYPTIAN    = 2,
    SVC_PRAYER_METHOD_KARACHI     = 3,
    SVC_PRAYER_METHOD_UMM_AL_QURA = 4,
    SVC_PRAYER_METHOD_JAFARIA     = 5,
    SVC_PRAYER_METHOD_COUNT       = 6,
} svc_prayer_method_t;

typedef enum {
    SVC_PRAYER_MADHAB_STANDARD = 0,
    SVC_PRAYER_MADHAB_HANAFI   = 1,
    SVC_PRAYER_MADHAB_COUNT    = 2,
} svc_prayer_madhab_t;

typedef enum {
    SVC_PRAYER_HIGHLAT_AUTO          = 0,
    SVC_PRAYER_HIGHLAT_NONE          = 1,
    SVC_PRAYER_HIGHLAT_MIDDLE_NIGHT  = 2,
    SVC_PRAYER_HIGHLAT_SEVENTH       = 3,
    SVC_PRAYER_HIGHLAT_ANGLE_BASED   = 4,
} svc_prayer_highlat_t;

/* Output. All times are seconds since local midnight (0..86399).
   If any prayer cannot be calculated (extreme latitude), valid is false
   and the numeric fields are 0. */
typedef struct {
    bool     valid;
    uint32_t fajr_sec;
    uint32_t sunrise_sec;
    uint32_t dhuhr_sec;
    uint32_t zawal_start_sec;
    uint32_t asr_yellow_sec;
    uint32_t sunset_sec;
    uint32_t zawal_sec;
    uint32_t asr_sec;
    uint32_t maghrib_sec;
    uint32_t isha_sec;
    uint32_t midnight_sec;
} svc_prayer_times_t;

/*
 * Calculate prayer times for a single day.
 *
 * Returns:
 *   ESP_OK                : success (out->valid is true)
 *   ESP_ERR_INVALID_ARG   : out NULL, or lat/lon/date/method/madhab out of range
 *   ESP_ERR_INVALID_STATE : calculation failed (polar day/night); out->valid is false
 */
esp_err_t svc_prayer_engine_calculate(
    int year,
    int month,
    int day,
    double latitude,
    double longitude,
    double timezone_offset,
    svc_prayer_method_t method,
    svc_prayer_madhab_t madhab,
    svc_prayer_highlat_t highlat,
    svc_prayer_times_t *out
);

#ifdef __cplusplus
}
#endif
