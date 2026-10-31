/*
 * adhan_calculator.c
 *
 * ChronoLink V4 OS -- Adhan tier calculator (internal).
 *
 * A pure function of (config, date, tz offset): no static state, no locks,
 * no logging. Extracted verbatim from the monolithic recalculate() so the
 * observable behavior of the service is unchanged -- every log line, the
 * timetable tripwire, the cache write, and the publish stay in the parent.
 */
#include "adhan_calculator.h"

#include <stddef.h>
#include <string.h>

esp_err_t adhan_calculator_run(const svc_config_t *cfg,
                               uint32_t date_yyyymmdd,
                               int16_t tz_offset_minutes,
                               adhan_result_t *out)
{
    if (cfg == NULL || out == NULL) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));

    /* date_yyyymmdd is the service's own encoding (year*10000 + month*100
       + day), so the decomposition is lossless for any real date. */
    int year  = (int)(date_yyyymmdd / 10000U);
    int month = (int)((date_yyyymmdd / 100U) % 100U);
    int day   = (int)(date_yyyymmdd % 100U);

    /* One fixed offset for the whole local day (the noon offset, DST-safe). */
    double tz_off = (double)tz_offset_minutes / 60.0;

    /* Cast config enums to prayer-engine enums (values 0..N match). */
    svc_prayer_method_t  method  = (svc_prayer_method_t)cfg->method;
    svc_prayer_madhab_t  madhab  = (svc_prayer_madhab_t)cfg->madhab;
    svc_prayer_highlat_t highlat = (svc_prayer_highlat_t)cfg->high_lat_rule;

    esp_err_t err = svc_prayer_engine_calculate(
        year, month, day,
        cfg->latitude, cfg->longitude, tz_off,
        method, madhab, highlat,
        cfg->custom_fajr_angle, cfg->custom_isha_angle,
        &out->raw);
    if (err != ESP_OK) return err;

    /* Tier 2: adhan = astronomical + user offset. No wrap, per the engine
       contract (a positive offset near 23:59 may exceed 86399). */
    out->adhan_sec[SVC_CONFIG_PRAYER_FAJR] =
        out->raw.fajr_sec    + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_FAJR]    * 60U;
    out->adhan_sec[SVC_CONFIG_PRAYER_DHUHR] =
        out->raw.dhuhr_sec   + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_DHUHR]   * 60U;
    out->adhan_sec[SVC_CONFIG_PRAYER_ASR] =
        out->raw.asr_sec     + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_ASR]     * 60U;
    out->adhan_sec[SVC_CONFIG_PRAYER_MAGHRIB] =
        out->raw.maghrib_sec + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_MAGHRIB] * 60U;
    out->adhan_sec[SVC_CONFIG_PRAYER_ISHA] =
        out->raw.isha_sec    + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_ISHA]    * 60U;

    return ESP_OK;
}
