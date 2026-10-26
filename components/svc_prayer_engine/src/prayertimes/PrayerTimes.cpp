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

/* -------------------------------------------------------------------------
 * Moonsighting Committee seasonal helpers.
 *
 * Faithful port of batoulapps/adhan-js Astronomical.ts. Produces the number
 * of minutes before sunrise (morning) or after sunset (evening) that
 * constitutes the "safe" twilight bound for the given latitude and day.
 * The seasonal tables interpolate across six segments of the year, anchored
 * on the winter solstice (roughly). These are used as BOUNDS in calculate(),
 * not as a blend: if the angle-based time is invalid or violates the bound,
 * the safe value is used instead.
 * ------------------------------------------------------------------------- */

/* Days since the winter solstice, mod 365 or 366. Anchor is 10 days after
   Jan 1 for the north, 172 (or 173 in leap) days before Jan 1 for the south
   (equivalent to adhan-js southernOffset). dayOfYear is 1..365 (or 1..366). */
static int days_since_solstice(int year, int month, int day, double latitude)
{
    static const int cum_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool is_leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
    int doy = cum_days[month - 1] + day + ((is_leap && month > 2) ? 1 : 0);

    int days_in_year = is_leap ? 366 : 365;
    int dyy;
    if (latitude >= 0.0) {
        dyy = (doy + 10) % days_in_year;
    } else {
        int southern_offset = is_leap ? 173 : 172;
        dyy = doy - southern_offset;
        if (dyy < 0) dyy += days_in_year;
    }
    return dyy;
}

/* Piecewise interpolation over dyy, shared by both twilight helpers. */
static double interpolate_season(double a, double b, double c, double d, int dyy)
{
    if (dyy < 91) {
        return a + ((b - a) / 91.0) * dyy;
    } else if (dyy < 137) {
        return b + ((c - b) / 46.0) * (dyy - 91);
    } else if (dyy < 183) {
        return c + ((d - c) / 46.0) * (dyy - 137);
    } else if (dyy < 229) {
        return d + ((c - d) / 46.0) * (dyy - 183);
    } else if (dyy < 275) {
        return c + ((b - c) / 46.0) * (dyy - 229);
    } else {
        return b + ((a - b) / 91.0) * (dyy - 275);
    }
}

/* Minutes before sunrise for Moonsighting safe Fajr bound. */
static double season_adj_fajr(double latitude, int dyy)
{
    double abs_lat = fabs(latitude);
    double a = 75.0 + (28.65 / 55.0) * abs_lat;
    double b = 75.0 + (19.44 / 55.0) * abs_lat;
    double c = 75.0 + (32.74 / 55.0) * abs_lat;
    double d = 75.0 + (48.10 / 55.0) * abs_lat;
    return interpolate_season(a, b, c, d, dyy);
}

/* Minutes after sunset for Moonsighting safe Isha bound (Shafaq General). */
static double season_adj_isha(double latitude, int dyy)
{
    double abs_lat = fabs(latitude);
    double a = 75.0 + (25.60 / 55.0) * abs_lat;
    double b = 75.0 + ( 2.05 / 55.0) * abs_lat;
    double c = 75.0 - ( 9.21 / 55.0) * abs_lat;
    double d = 75.0 + ( 6.14 / 55.0) * abs_lat;
    return interpolate_season(a, b, c, d, dyy);
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

    // Moonsighting Committee seasonal bound (adhan-js parity).
    // The seasonal tables give a "safe" twilight bound as a fixed number of
    // minutes before sunrise (Fajr) or after sunset (Isha). We clamp the
    // angle-based value to this bound: Fajr cannot be earlier than safeFajr,
    // Isha cannot be later than safeIsha. When the angle yields NaN (polar
    // regions where the sun never reaches the angle), the safe bound is used
    // directly.
    if (method.is_moonsighting) {
        int dyy = days_since_solstice(date.year, date.month, date.day, coords.latitude);
        double adj_fajr_min = season_adj_fajr(coords.latitude, dyy);
        double adj_isha_min = season_adj_isha(coords.latitude, dyy);

        // adhan-js rounds the adjustment to the nearest second:
        //   sunrise + round(adj_min * -60)   [sunrise/adjustment in seconds]
        // Our sunrise/isha are in hours, so divide by 3600 to convert the
        // rounded seconds back to hours.
        double adj_fajr_sec = round(adj_fajr_min * 60.0);
        double adj_isha_sec = round(adj_isha_min * 60.0);

        double safe_fajr = sunrise - adj_fajr_sec / 3600.0;
        double safe_isha = sunset  + adj_isha_sec / 3600.0;

        if (isnan(fajr) || safe_fajr > fajr) fajr = safe_fajr;
        if (isnan(isha) || safe_isha < isha) isha = safe_isha;
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

    double asr_yellowing = sun_angle_time_iterated(jd, mid, coords.latitude, coords.longitude,
                                                   -5.0, 1.0);

    result.fajr = fix_hour(fajr + offset);
    result.sunrise = fix_hour(sunrise + offset);
    result.dhuhr = fix_hour(mid + offset);
    result.zawal = result.dhuhr;
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
