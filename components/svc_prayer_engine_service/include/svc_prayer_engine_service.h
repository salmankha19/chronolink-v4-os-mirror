/*
 * svc_prayer_engine_service.h
 *
 * ChronoLink V4 OS -- Prayer engine runtime service (public API)
 *
 * Glue layer between svc_config (location/method inputs), svc_time
 * (current date), and svc_prayer_engine (pure C++ math).
 *
 * Consumers subscribe to EVENT_PRAYER_TIMES_UPDATED on the event bus.
 * IMPORTANT: that event is a SIGNAL ONLY -- the payload is truncated
 * (event_t.data is 32 bytes, svc_prayer_engine_times_t is 120). Do NOT
 * memcpy the payload. On receiving the event, call
 * svc_prayer_engine_service_get_times() to read the full snapshot.
 *
 * Does NOT drive audio, schedule RTC alarms, or update the display
 * directly. Those are separate concerns in later PRs.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "svc_config.h"   /* SVC_CONFIG_PRAYER_COUNT, season_cfg_t (jamat tiers) */

#ifdef __cplusplus
extern "C" {
#endif

/* Final prayer times after adhan offsets are applied (seconds since midnight). */
typedef struct {
    bool     valid;
    uint32_t date_yyyymmdd;      /* cache key: recalculate when this changes */
    int16_t  tz_offset_minutes;  /* cache key: recalculate when this changes;
                                    = local - UTC at noon of target date */
    uint8_t  season;             /* 0 = winter, 1 = summer (for diagnostics) */

    /* -----------------------------------------------------------------
     * Tier 1: ASTRONOMICAL local time. Engine output: astronomical
     * calculation (sun angle / declination / equation of time) +
     * longitude correction + timezone offset, expressed as local civil
     * time (seconds since local midnight). NO user-configurable offsets
     * applied -- this is what the astronomy says the prayer instant is.
     * ----------------------------------------------------------------- */
    uint32_t astronomical_fajr_sec;
    uint32_t astronomical_dhuhr_sec;
    uint32_t astronomical_asr_sec;
    uint32_t astronomical_maghrib_sec;
    uint32_t astronomical_isha_sec;

    /* -----------------------------------------------------------------
     * Tier 2: ADHAN. Astronomical + adhan_offset_min[]. What the mosque
     * actually calls. (Field names kept as `*_sec` for backward
     * compatibility with pre-5.1a consumers.)
     * ----------------------------------------------------------------- */
    uint32_t fajr_sec;
    uint32_t dhuhr_sec;
    uint32_t asr_sec;
    uint32_t maghrib_sec;
    uint32_t isha_sec;

    /* -----------------------------------------------------------------
     * Tier 3: JAMAT. Adhan (or sunrise/midnight) + seasonal offset +
     * rounding. UINT32_MAX means "no jamat this season" (anchor
     * DISABLED) or "cannot be computed for this date".
     * ----------------------------------------------------------------- */
    uint32_t jamat_sec[SVC_CONFIG_PRAYER_COUNT];
    /* Tomorrow's jamat, same order and sentinel. Consumers compare
       jamat_tomorrow_sec[p] != jamat_sec[p] to warn the user today
       ("Tomorrow Asr jamat is 3:50"). Computed on the same recalculate
       using tomorrow's own season and timezone offset, so midnight
       rollover, season transitions, and DST transitions are all
       reflected consistently. */
    uint32_t jamat_tomorrow_sec[SVC_CONFIG_PRAYER_COUNT];

    /* -----------------------------------------------------------------
     * Auxiliary -- astronomical events, not prayers. No adhan_offset
     * applied (raw engine output).
     * ----------------------------------------------------------------- */
    uint32_t sunrise_sec;
    uint32_t sunset_sec;
    uint32_t midnight_sec;
    uint32_t zawal_start_sec;
    uint32_t asr_yellow_sec;

    /* -----------------------------------------------------------------
     * Ramadan endpoints (astronomical tier).
     *   sehri_end_sec = astronomical_fajr_sec (pre adhan-offset)
     *   iftar_sec     = sunset_sec
     * ----------------------------------------------------------------- */
    uint32_t sehri_end_sec;
    uint32_t iftar_sec;
} svc_prayer_engine_times_t;

/*
 * Start the service. Subscribes to EVENT_TIME_UPDATE and
 * EVENT_TIME_DATE_CHANGED, and does an initial calculation immediately.
 * A 60 s periodic wakeup self-heals if an event is dropped; on each wake
 * the service compares the current local date and noon UTC offset
 * against the cache and recalculates if either has changed. The noon
 * offset is DST-safe: a day's prayer times all use the same offset even
 * on a transition day.
 *
 * Non-blocking. Idempotent.
 *
 * Returns ESP_FAIL only if the internal mutex could not be created.
 * Missing config (unset location) is logged and leaves the service in
 * a "not valid" state until config is set; it does not fail startup.
 */
esp_err_t svc_prayer_engine_service_start(void);

/*
 * Force a recalculation now (in the caller's task). Intended for the
 * web UI: after the user saves a new location or method, call this.
 *
 * Returns ESP_OK on success (times available and published),
 * ESP_ERR_INVALID_STATE if svc_config has no location set,
 * or an error from svc_time / svc_prayer_engine.
 */
esp_err_t svc_prayer_engine_service_recalculate(void);

/*
 * Get a snapshot of the current prayer times. Returns ESP_OK and
 * fills `out` if a calculation has succeeded at least once. Returns
 * ESP_ERR_INVALID_STATE if the service has not started or the last
 * calculation failed (e.g., no location configured, polar night).
 */
esp_err_t svc_prayer_engine_service_get_times(svc_prayer_engine_times_t *out);

#ifdef __cplusplus
}
#endif
