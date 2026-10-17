/*
 * svc_prayer_engine.cpp
 *
 * ChronoLink V4 OS -- C wrapper over the pure C++ prayer math library.
 *
 * This file bridges the C-visible API (svc_prayer_engine.h) to the
 * C++ implementation (prayertimes/PrayerTimes.hpp). It must compile
 * as C++ because it includes the .hpp header and calls the namespaced
 * class member PrayerTimesCalculator::calculate().
 */
#include "svc_prayer_engine.h"
#include "prayertimes/PrayerTimes.hpp"

#include <math.h>

extern "C" {

static uint32_t hours_to_sec(double h)
{
    if (!isfinite(h)) return 0;
    h = fmod(h, 24.0);
    if (h < 0.0) h += 24.0;
    double s = h * 3600.0 + 0.5;
    if (s < 0.0) s = 0.0;
    if (s > 86399.0) s = 86399.0;
    return (uint32_t)s;
}

esp_err_t svc_prayer_engine_calculate(
    int year,
    int month,
    int day,
    double latitude,
    double longitude,
    double timezone_offset,
    svc_prayer_method_t method,
    svc_prayer_madhab_t madhab,
    svc_prayer_times_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    *out = svc_prayer_times_t{};

    /* NaN-safe range checks for the doubles. */
    if (!(latitude  >= -90.0  && latitude  <=  90.0))  return ESP_ERR_INVALID_ARG;
    if (!(longitude >= -180.0 && longitude <= 180.0))  return ESP_ERR_INVALID_ARG;
    if (year < 1900 || year > 2100) return ESP_ERR_INVALID_ARG;
    if (month < 1 || month > 12)    return ESP_ERR_INVALID_ARG;
    if (day < 1   || day > 31)      return ESP_ERR_INVALID_ARG;
    if ((unsigned)method >= (unsigned)SVC_PRAYER_METHOD_COUNT) return ESP_ERR_INVALID_ARG;
    if ((unsigned)madhab >= (unsigned)SVC_PRAYER_MADHAB_COUNT) return ESP_ERR_INVALID_ARG;

    prayertimes::Method m{};
    switch (method) {
    case SVC_PRAYER_METHOD_MWL:         m = prayertimes::MWL;         break;
    case SVC_PRAYER_METHOD_ISNA:        m = prayertimes::ISNA;        break;
    case SVC_PRAYER_METHOD_EGYPTIAN:    m = prayertimes::EGYPTIAN;    break;
    case SVC_PRAYER_METHOD_KARACHI:     m = prayertimes::KARACHI;     break;
    case SVC_PRAYER_METHOD_UMM_AL_QURA: m = prayertimes::UMM_AL_QURA; break;
    case SVC_PRAYER_METHOD_JAFARIA:     m = prayertimes::JAFARIA;     break;
    default: return ESP_ERR_INVALID_ARG;
    }

    if (madhab == SVC_PRAYER_MADHAB_HANAFI) {
        m.asr_shadow_factor = 2.0;
    }

    prayertimes::Date d{ year, month, day };
    prayertimes::Coordinates c{ latitude, longitude };

    prayertimes::PrayerTimes t = prayertimes::PrayerTimesCalculator::calculate(
        d, c, timezone_offset, m);

    if (!t.valid) {
        return ESP_ERR_INVALID_STATE;
    }

    out->fajr_sec     = hours_to_sec(t.fajr);
    out->sunrise_sec  = hours_to_sec(t.sunrise);
    out->dhuhr_sec    = hours_to_sec(t.dhuhr);
    out->zawal_sec    = hours_to_sec(t.zawal);
    out->asr_sec      = hours_to_sec(t.asr);
    out->maghrib_sec  = hours_to_sec(t.maghrib);
    out->isha_sec     = hours_to_sec(t.isha);
    out->midnight_sec = hours_to_sec(t.midnight);
    out->valid        = true;

    return ESP_OK;
}

} /* extern "C" */
