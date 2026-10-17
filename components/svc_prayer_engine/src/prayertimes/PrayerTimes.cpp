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
    SolarPosition sp = sun_position(jd);
    double mid = mid_day(jd);

    // Sunrise/sunset at -0.833 degrees (refraction + sun disk)
    double sunrise = sun_angle_time(0.833, mid, coords.latitude, sp.declination, -1.0);
    double sunset = sun_angle_time(0.833, mid, coords.latitude, sp.declination, 1.0);

    if (isnan(sunrise) || isnan(sunset)) {
        return result;  // polar day/night
    }

    double fajr = sun_angle_time(method.fajr_angle, mid, coords.latitude, sp.declination, -1.0);
    double asr = asr_time(mid, coords.latitude, sp.declination, method.asr_shadow_factor);

    double maghrib;
    if (method.maghrib_angle > 0.0) {
        maghrib = sun_angle_time(method.maghrib_angle, mid, coords.latitude, sp.declination, 1.0);
        if (isnan(maghrib)) maghrib = sunset;
    } else {
        maghrib = sunset;
    }

    double isha;
    if (method.isha_interval > 0) {
        isha = maghrib + method.isha_interval / 60.0;
    } else {
        isha = sun_angle_time(method.isha_angle, mid, coords.latitude, sp.declination, 1.0);
    }

    // High-latitude fallback
    if (isnan(fajr) || isnan(isha)) {
        double night = 24.0 - (sunset - sunrise);
        HighLatitudeRule effective_rule = highlat_rule;

        if (effective_rule == HighLatitudeRule::AUTO) {
            // We only reach this block if an angle was unreachable (NaN).
            // Therefore NONE is not a valid fallback here. We must apply a rule.
            double abs_lat = fabs(coords.latitude);
            if (abs_lat < 55.0) effective_rule = HighLatitudeRule::MIDDLE_NIGHT;
            else                effective_rule = HighLatitudeRule::SEVENTH_OF_THE_NIGHT;
        }

        if (effective_rule == HighLatitudeRule::MIDDLE_NIGHT) {
            if (isnan(fajr)) fajr = sunrise - night / 2.0;
            if (isnan(isha)) isha = sunset + night / 2.0;
        } else if (effective_rule == HighLatitudeRule::SEVENTH_OF_THE_NIGHT) {
            if (isnan(fajr)) fajr = sunrise - night / 7.0;
            if (isnan(isha)) isha = sunset + night / 7.0;
        } else if (effective_rule == HighLatitudeRule::ANGLE_BASED) {
            // Formula from PrayTimes.org
            double fajr_angle = method.fajr_angle;
            double isha_angle = method.isha_angle;
            if (isnan(fajr)) fajr = sunrise - night * (fajr_angle / 60.0);
            if (isnan(isha) && isha_angle > 0) isha = sunset + night * (isha_angle / 60.0);
        } else {
            // NONE - leave as NaN (will fail the final validity check)
        }
    }

    // Offset to local clock time
    double offset = timezone_offset - coords.longitude / 15.0;

    result.fajr = fix_hour(fajr + offset);
    result.sunrise = fix_hour(sunrise + offset);
    result.dhuhr = fix_hour(mid + offset);
    result.zawal = result.dhuhr;
    result.asr = fix_hour(asr + offset);
    result.maghrib = fix_hour(maghrib + offset);
    result.isha = fix_hour(isha + offset);
    result.midnight = fix_hour(sunset + offset + (fajr + 24.0 - sunset) / 2.0);

    if (isnan(result.fajr) || isnan(result.sunrise) || isnan(result.dhuhr) ||
        isnan(result.asr) || isnan(result.maghrib) || isnan(result.isha)) {
        return result;
    }

    result.valid = true;
    return result;
}

} // namespace prayertimes
