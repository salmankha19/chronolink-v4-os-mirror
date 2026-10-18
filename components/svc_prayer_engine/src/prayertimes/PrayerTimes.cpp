/*
 * PrayerTimes.cpp
 *
 * ChronoLink V4 OS -- Standalone Prayer Time Calculation Library
 *
 * Based on the PrayTimes.org astronomical algorithm.
 */
#include "prayertimes/PrayerTimes.hpp"
#include <math.h>

namespace prayertimes {

// --- Math Helpers (degrees) ---

static double dsin(double d) { return sin(d * M_PI / 180.0); }
static double dcos(double d) { return cos(d * M_PI / 180.0); }
static double dtan(double d) { return tan(d * M_PI / 180.0); }

static double darcsin(double x) { return asin(x) * 180.0 / M_PI; }
static double darccos(double x) { return acos(x) * 180.0 / M_PI; }
static double darctan2(double y, double x) { return atan2(y, x) * 180.0 / M_PI; }
static double darccot(double x) { return atan2(1.0, x) * 180.0 / M_PI; }

static double fix_angle(double a) {
    a = fmod(a, 360.0);
    return (a < 0) ? a + 360.0 : a;
}

static double fix_hour(double a) {
    /* Propagate NaN: fmod(NaN, 24) is unspecified per C standard and
       picolibc returns 0 instead of NaN, which silently turns "sun never
       reached this angle" into "00:00:00". */
    if (isnan(a)) return a;
    a = fmod(a, 24.0);
    return (a < 0) ? a + 24.0 : a;
}

// --- Astronomical Calculations ---

double PrayerTimesCalculator::julian_date(int year, int month, int day) {
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    double a = floor(year / 100.0);
    double b = 2.0 - a + floor(a / 4.0);
    return floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) + day + b - 1524.5;
}

PrayerTimesCalculator::SolarPosition PrayerTimesCalculator::sun_position(double jd) {
    double d = jd - 2451545.0;
    double g = fix_angle(357.529 + 0.98560028 * d);
    double q = fix_angle(280.459 + 0.98564736 * d);
    double L = fix_angle(q + 1.915 * dsin(g) + 0.020 * dsin(2 * g));
    double e = 23.439 - 0.00000036 * d;
    double RA = darctan2(dcos(e) * dsin(L), dcos(L)) / 15.0;
    double eqt = q / 15.0 - fix_hour(RA);
    double decl = darcsin(dsin(e) * dsin(L));

    SolarPosition sp;
    sp.declination = decl;
    sp.equation = eqt;
    return sp;
}

double PrayerTimesCalculator::mid_day(double jd) {
    SolarPosition sp = sun_position(jd);
    return fix_hour(12.0 - sp.equation);
}

double PrayerTimesCalculator::sun_angle_time(double angle, double time, double lat, double decl, double direction) {
    double numerator = -dsin(angle) - dsin(decl) * dsin(lat);
    double denominator = dcos(decl) * dcos(lat);
    double ratio = numerator / denominator;

    if (ratio > 1.0 || ratio < -1.0) {
        return NAN; // Sun never reaches this angle (high latitude)
    }

    double t = darccos(ratio) / 15.0;
    return time + direction * t;
}

double PrayerTimesCalculator::asr_time(double time, double lat, double decl, double shadow_factor) {
    // Angle when shadow length equals shadow_factor
    double angle = -darccot(shadow_factor + dtan(fabs(lat - decl)));
    return sun_angle_time(angle, time, lat, decl, 1.0); // Asr is in the afternoon (direction +1)
}

/* Iterated solar noon. Re-samples the equation of time at the estimated
   transit UT hour until the estimate converges. Returns local mean time
   (fractional hours, NOT yet offset by tz). The lon offset is inside
   the sample instant because the transit happens at local noon, which
   is lon/360 hours away from 0h UT. */
double PrayerTimesCalculator::mid_day_iterated(double jd, double lon)
{
    double t = mid_day(jd);       /* seed from the 0h sample */
    for (int i = 0; i < 3; i++) {
        double jd_sample = jd - lon / 360.0 + t / 24.0;
        SolarPosition sp = sun_position(jd_sample);
        double t_next = fix_hour(12.0 - sp.equation);
        double delta = fabs(t_next - t);
        t = t_next;
        if (delta < (1.0 / 3600.0)) break;   /* <1 second */
    }
    return t;
}

/* Iterated angle-event time. Re-samples the declination at the event's
   estimated UT hour until the time converges. `direction` is -1 for
   morning events (fajr, sunrise) and +1 for afternoon events (maghrib,
   sunset, isha). Returns local mean time (fractional hours, no tz). */
double PrayerTimesCalculator::sun_angle_time_iterated(
    double jd, double mid, double lat, double lon,
    double angle, double direction)
{
    /* Seed with the 0h-UT declination at the transit instant. */
    double jd_transit = jd - lon / 360.0 + mid / 24.0;
    SolarPosition sp_seed = sun_position(jd_transit);
    double t = sun_angle_time(angle, mid, lat, sp_seed.declination, direction);

    for (int i = 0; i < 3; i++) {
        if (isnan(t)) break;      /* unreachable angle: let caller's NaN path handle it */
        double jd_sample = jd - lon / 360.0 + t / 24.0;
        SolarPosition sp = sun_position(jd_sample);
        double t_next = sun_angle_time(angle, mid, lat, sp.declination, direction);
        if (isnan(t_next)) { t = t_next; break; }
        double delta = fabs(t_next - t);
        t = t_next;
        if (delta < (1.0 / 3600.0)) break;
    }
    return t;
}

/* Iterated Asr. Same pattern as sun_angle_time_iterated, but the shadow
   angle is recomputed each pass from the sampled declination. */
double PrayerTimesCalculator::asr_time_iterated(
    double jd, double mid, double lat, double lon,
    double shadow_factor)
{
    double jd_transit = jd - lon / 360.0 + mid / 24.0;
    SolarPosition sp_seed = sun_position(jd_transit);
    double t = asr_time(mid, lat, sp_seed.declination, shadow_factor);

    for (int i = 0; i < 3; i++) {
        if (isnan(t)) break;
        double jd_sample = jd - lon / 360.0 + t / 24.0;
        SolarPosition sp = sun_position(jd_sample);
        double t_next = asr_time(mid, lat, sp.declination, shadow_factor);
        if (isnan(t_next)) { t = t_next; break; }
        double delta = fabs(t_next - t);
        t = t_next;
        if (delta < (1.0 / 3600.0)) break;
    }
    return t;
}

PrayerTimes PrayerTimesCalculator::calculate(
    const Date& date,
    const Coordinates& coords,
    double timezone_offset,
    const Method& method,
    HighLatitudeRule highlat_rule
) {
    PrayerTimes result{};
    result.valid = false;

    double jd = julian_date(date.year, date.month, date.day);
    /* Iterated solar transit: re-samples the equation of time at the
       estimate of transit UT hour. ~8s more accurate at equinox vs
       the old 0h-UT sample. */
    double mid = mid_day_iterated(jd, coords.longitude);

    /* Sunrise/sunset at 0.833 deg depression (refraction + sun disk),
       iterated per event so the declination is sampled near the event
       instant rather than at 0h UT. */
    double sunrise = sun_angle_time_iterated(jd, mid, coords.latitude, coords.longitude, 0.833, -1.0);
    double sunset  = sun_angle_time_iterated(jd, mid, coords.latitude, coords.longitude, 0.833,  1.0);

    if (isnan(sunrise) || isnan(sunset)) {
        return result;  // polar day/night
    }

    double fajr = sun_angle_time_iterated(jd, mid, coords.latitude, coords.longitude,
                                          method.fajr_angle, -1.0);
    double asr  = asr_time_iterated(jd, mid, coords.latitude, coords.longitude,
                                    method.asr_shadow_factor);

    double maghrib;
    if (method.maghrib_angle > 0.0) {
        maghrib = sun_angle_time_iterated(jd, mid, coords.latitude, coords.longitude,
                                          method.maghrib_angle, 1.0);
        if (isnan(maghrib)) maghrib = sunset;
    } else {
        maghrib = sunset;
    }

    double isha;
    if (method.isha_interval > 0) {
        isha = maghrib + method.isha_interval / 60.0;
    } else {
        isha = sun_angle_time_iterated(jd, mid, coords.latitude, coords.longitude,
                                       method.isha_angle, 1.0);
    }

    // High-latitude fallback
    HighLatitudeRule effective_rule = highlat_rule;

    if (effective_rule == HighLatitudeRule::AUTO) {
        // For high latitudes (>48), one-seventh is the most practical default.
        // Below 48, middle-of-night is only sensible while the twilight angles
        // are reachable all year. A twilight angle becomes unreachable at the
        // summer solstice when |lat| > 90 - 23.44 - angle (~66.5 - angle), e.g.
        // Egyptian 19.5 deg from ~47 deg N. In that band middle-of-night would
        // place Fajr at ~midnight (before Isha), so use one-seventh there too.
        // This test depends only on latitude + method (not the date), so the
        // chosen rule never flips mid-year.
        double abs_lat = fabs(coords.latitude);
        bool angle_unreachable_in_summer =
            (method.fajr_angle > 0.0 && abs_lat > 66.5 - method.fajr_angle) ||
            (method.isha_interval == 0 && method.isha_angle > 0.0 &&
             abs_lat > 66.5 - method.isha_angle);
        if (abs_lat > 48.0 || angle_unreachable_in_summer)
            effective_rule = HighLatitudeRule::SEVENTH_OF_THE_NIGHT;
        else
            effective_rule = HighLatitudeRule::MIDDLE_NIGHT;
    }

    double night = 24.0 - (sunset - sunrise);
    double fajr_portion = 0, isha_portion = 0;

    if (effective_rule == HighLatitudeRule::MIDDLE_NIGHT) {
        fajr_portion = night / 2.0;
        isha_portion = night / 2.0;
    } else if (effective_rule == HighLatitudeRule::SEVENTH_OF_THE_NIGHT) {
        fajr_portion = night / 7.0;
        isha_portion = night / 7.0;
    } else if (effective_rule == HighLatitudeRule::ANGLE_BASED) {
        fajr_portion = night * (method.fajr_angle / 60.0);
        if (method.isha_angle > 0) isha_portion = night * (method.isha_angle / 60.0);
    }

    // Apply clamping (PrayTimes.org style)
    if (fajr_portion > 0) {
        if (isnan(fajr) || (sunrise - fajr) > fajr_portion) fajr = sunrise - fajr_portion;
    }
    if (isha_portion > 0 && method.isha_interval == 0) {
        // Measured from Maghrib, not sunset: for methods with a Maghrib angle
        // (Jafaria, 4 deg) Maghrib is later than sunset, and a sunset-based
        // limit could put Isha before Maghrib at high latitude.
        if (isnan(isha) || (isha - maghrib) > isha_portion) isha = maghrib + isha_portion;
    }

    // Islamic midnight: halfway between sunset and the (clamped) Fajr,
    // so the displayed midnight is consistent with the displayed Fajr.
    // It is never earlier than Isha (floored at Isha). Using max() keeps the
    // value continuous day to day; the old "switch to a sunrise-based
    // definition when midnight < isha" jumped by up to ~2 h near the
    // clamp boundary. Sunrise-based only if Fajr itself is NaN.
    double midnight;
    if (isnan(fajr)) {
        midnight = sunset + (sunrise + 24.0 - sunset) / 2.0;
    } else {
        midnight = sunset + (fajr + 24.0 - sunset) / 2.0;
        if (midnight < isha) midnight = isha;
    }

    // Offset to local clock time
    double offset = timezone_offset - coords.longitude / 15.0;

    double zawal_start = mid - (10.0 / 60.0); // 10 minutes before Dhuhr
    double asr_yellowing = sun_angle_time_iterated(jd, mid, coords.latitude, coords.longitude,
                                                   -5.0, 1.0);

    result.fajr = fix_hour(fajr + offset);
    result.sunrise = fix_hour(sunrise + offset);
    result.dhuhr = fix_hour(mid + offset);
    result.zawal = result.dhuhr;
    result.zawal_start = fix_hour(zawal_start + offset);
    result.asr = fix_hour(asr + offset);
    result.asr_yellowing = fix_hour(asr_yellowing + offset);
    result.maghrib = fix_hour(maghrib + offset);
    result.sunset = fix_hour(sunset + offset);
    // Night events are expressed relative to the (wrapped) Maghrib / sunset,
    // not wrapped themselves: they may exceed 24h (post-midnight), and the
    // Maghrib->Isha interval stays correct even if Maghrib wraps past 24h
    // (civil time far ahead of solar time).
    result.isha     = result.maghrib + (isha - maghrib);
    result.midnight = result.sunset  + (midnight - sunset);

    if (isnan(result.fajr) || isnan(result.sunrise) || isnan(result.dhuhr) ||
        isnan(result.asr) || isnan(result.maghrib) || isnan(result.isha)) {
        return result;
    }

    result.valid = true;
    return result;
}

} // namespace prayertimes
