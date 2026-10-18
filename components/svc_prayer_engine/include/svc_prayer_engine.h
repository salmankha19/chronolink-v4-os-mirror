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

/* Calculation methods. Values 0..5 match svc_config_method_t exactly
   (Jafaria included); the service layer casts config -> engine 1:1. */
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

/* Output. Most times are seconds since local midnight (0..86399).
   Isha and Islamic midnight may be up to 172799 (i.e. after midnight,
   same day +1) when they fall after 00:00. If any prayer cannot be
   calculated (extreme latitude), valid is false and numeric fields are 0.

   asr_yellow_sec uses UINT32_MAX as a sentinel for "sun does not reach
   5 degrees above the horizon today" (high-latitude winter), distinct
   from 0 (00:00:00). Consumers should treat UINT32_MAX as unavailable,
   not as a real time. */
typedef struct {
    bool     valid;
    uint32_t fajr_sec;
    uint32_t sunrise_sec;
    uint32_t dhuhr_sec;
    uint32_t asr_yellow_sec;
    uint32_t sunset_sec;
    uint32_t zawal_sec;
    uint32_t asr_sec;
    uint32_t maghrib_sec;
    uint32_t isha_sec;
    uint32_t midnight_sec;
} svc_prayer_times_t;

/* Day-rollover contract.
 *
 * Fajr / Sunrise / Dhuhr / Asr / Maghrib / Sunset / Isha / Midnight are
 * expressed in seconds since local midnight of the *requested* date. Two
 * fields may legitimately exceed 86399:
 *
 *   - isha_sec    : Isha can fall after local midnight in summer at high
 *                   latitude. Legal range is 0 .. 172799.
 *   - midnight_sec: Islamic midnight likewise; same range.
 *
 * In addition, svc_prayer_engine_service adds per-prayer adhan offsets to
 * every field without wrap. A positive offset applied to a prayer near
 * 23:59 therefore pushes that field to >= 86400 too.
 *
 * Any consumer that compares these values against a "current time since
 * local midnight" (for a "next prayer" indicator, an adhan scheduler, etc.)
 * MUST do day-aware comparison. A naive `if (now_sec < isha_sec)` is wrong
 * between local midnight and the actual Isha instant when isha_sec > 86399.
 *
 * Use the helper below to resolve a field into (day_offset, time_of_day):
 *     bool next_day;
 *     uint32_t tod;
 *     next_day = svc_prayer_time_resolve(t.isha_sec, &tod);
 *     // Now compare `tod` in [0, 86400) and adjust for next_day as needed.
 */
static inline bool svc_prayer_time_resolve(uint32_t sec, uint32_t *time_of_day_sec)
{
    if (time_of_day_sec) *time_of_day_sec = sec % 86400U;
    return sec >= 86400U;
}

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
