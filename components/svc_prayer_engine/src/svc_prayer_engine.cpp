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
    // Use isnan() not isfinite(): on this picolibc build, isfinite(NaN)
    // returns true, so !isfinite() would miss NaN and hit the UB cast
    // (uint32_t)NaN below. isnan() is proven working (PR 16).
    if (isnan(h)) return 0;
    // Round UP to the next whole minute (ceiling). A published prayer
    // time must never be earlier than the astronomical instant; ceiling
    // guarantees that. Convert hours -> minutes, ceil, then back to
    // seconds. Adhan offsets and jamat calculations in
    // svc_prayer_engine_service operate on the resulting minute-aligned
    // values.
    //
    // Do not wrap to 24h. Isha and Midnight can exceed 86400.
    // Clamp at 2 days (172799) just as a safety net.
    double m = h * 60.0;
    double s = ceil(m) * 60.0;
    if (s < 0.0) s = 0.0;
    if (s > 172799.0) s = 172799.0;
    return (uint32_t)s;
}

/* Days in the given month, accounting for leap years.
   Returns 0 if the month is out of range (1..12). */
static int days_in_month(int year, int month)
{
    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;

    if (month == 2) {
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return mdays[month - 1];
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
    svc_prayer_highlat_t highlat,
    double custom_fajr_angle,
    double custom_isha_angle,
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
    {
        int max_day = days_in_month(year, month);
        if (max_day == 0 || day > max_day) return ESP_ERR_INVALID_ARG;
    }
    if ((unsigned)method >= (unsigned)SVC_PRAYER_METHOD_COUNT) return ESP_ERR_INVALID_ARG;
    if ((unsigned)madhab >= (unsigned)SVC_PRAYER_MADHAB_COUNT) return ESP_ERR_INVALID_ARG;
    /* No SVC_PRAYER_HIGHLAT_COUNT enumerator exists by design (AUTO is a
       sentinel, not a real rule). Range check uses the last valid value. */
    if ((unsigned)highlat > (unsigned)SVC_PRAYER_HIGHLAT_ANGLE_BASED) return ESP_ERR_INVALID_ARG;

    prayertimes::Method m{};
    switch (method) {
    case SVC_PRAYER_METHOD_MWL:         m = prayertimes::MWL;         break;
    case SVC_PRAYER_METHOD_ISNA:        m = prayertimes::ISNA;        break;
    case SVC_PRAYER_METHOD_EGYPTIAN:    m = prayertimes::EGYPTIAN;    break;
    case SVC_PRAYER_METHOD_KARACHI:     m = prayertimes::KARACHI;     break;
    case SVC_PRAYER_METHOD_UMM_AL_QURA: m = prayertimes::UMM_AL_QURA; break;
    case SVC_PRAYER_METHOD_JAFARIA:      m = prayertimes::JAFARIA;      break;
    case SVC_PRAYER_METHOD_MOONSIGHTING: m = prayertimes::MOONSIGHTING; break;
    case SVC_PRAYER_METHOD_CUSTOM:
        m = prayertimes::MWL;
        m.fajr_angle = custom_fajr_angle;
        m.isha_angle = custom_isha_angle;
        m.is_moonsighting = false;
        break;
    default: return ESP_ERR_INVALID_ARG;
    }

    if (madhab == SVC_PRAYER_MADHAB_HANAFI) {
        m.asr_shadow_factor = 2.0;
    }

    /* Map C enum to C++ enum. The numeric values intentionally do NOT
       match (C AUTO=0, NONE=1; C++ AUTO=4, NONE=0), so the switch is
       required. */
    prayertimes::HighLatitudeRule hl = prayertimes::HighLatitudeRule::AUTO;
    switch (highlat) {
    case SVC_PRAYER_HIGHLAT_NONE:         hl = prayertimes::HighLatitudeRule::NONE; break;
    case SVC_PRAYER_HIGHLAT_MIDDLE_NIGHT: hl = prayertimes::HighLatitudeRule::MIDDLE_NIGHT; break;
    case SVC_PRAYER_HIGHLAT_SEVENTH:      hl = prayertimes::HighLatitudeRule::SEVENTH_OF_THE_NIGHT; break;
    case SVC_PRAYER_HIGHLAT_ANGLE_BASED:  hl = prayertimes::HighLatitudeRule::ANGLE_BASED; break;
    case SVC_PRAYER_HIGHLAT_AUTO:
    default:                              hl = prayertimes::HighLatitudeRule::AUTO; break;
    }

    prayertimes::Date d{ year, month, day };
    prayertimes::Coordinates c{ latitude, longitude };

    prayertimes::PrayerTimes t = prayertimes::PrayerTimesCalculator::calculate(
        d, c, timezone_offset, m, hl);

    if (!t.valid) {
        return ESP_ERR_INVALID_STATE;
    }

    out->fajr_sec     = hours_to_sec(t.fajr);
    out->sunrise_sec  = hours_to_sec(t.sunrise);
    out->dhuhr_sec    = hours_to_sec(t.dhuhr);
    /* On high-latitude days where the sun never reaches 5 degrees above
       the horizon, asr_yellowing is NaN. Distinguish that from a genuine
       00:00:00 using UINT32_MAX as the sentinel. Use isnan() not
       isfinite(): see hours_to_sec() comment above. */
    out->asr_yellow_sec = isnan(t.asr_yellowing)
                        ? UINT32_MAX
                        : hours_to_sec(t.asr_yellowing);
    out->sunset_sec      = hours_to_sec(t.sunset);
    out->zawal_sec    = hours_to_sec(t.zawal);
    out->asr_sec      = hours_to_sec(t.asr);
    out->maghrib_sec  = hours_to_sec(t.maghrib);
    out->isha_sec     = hours_to_sec(t.isha);
    out->midnight_sec = hours_to_sec(t.midnight);
    out->valid        = true;

    return ESP_OK;
}

} /* extern "C" */
