/*
 * PrayerTimes.hpp
 *
 * ChronoLink V4 OS -- Standalone Prayer Time Calculation Library
 *
 * This object holds the calculated times for a single day.
 * Most times are in decimal hours (0.0 to 24.0). However, Isha and
 * Islamic Midnight may exceed 24.0 if they fall after midnight.
 */
#pragma once

#include <stdint.h>
#include "Method.hpp"

namespace prayertimes {

enum class HighLatitudeRule {
    NONE = 0,
    MIDDLE_NIGHT = 1,
    SEVENTH_OF_THE_NIGHT = 2,
    ANGLE_BASED = 3,
    AUTO = 4
};

struct Coordinates {
    double latitude;
    double longitude;
};

struct Date {
    int year;
    int month; // 1-12
    int day;   // 1-31
};

struct PrayerTimes {
    // Core 5 prayers + Sunrise
    double fajr;
    double sunrise;
    double dhuhr;
    double asr;
    double maghrib; // Sunset
    double isha;

    // Additional times for Fiqh boundaries
    double zawal;    // Solar noon (sun at zenith, forbidden to pray)
    double midnight; // Islamic midnight (end of Isha)
    double zawal_start;    /* Start of Zawal (forbidden time) */
    double asr_yellowing;  /* Asr preferred end (sun 5 deg above horizon) */
    double sunset;         /* True astronomical sunset */

    // True if all calculations succeeded (e.g., not null in extreme latitudes)
    bool valid;
};

class PrayerTimesCalculator {
public:
    // Calculates prayer times for a given date and coordinates.
    // timezone_offset: Standard timezone offset from UTC (e.g., +5.0 for Pakistan, 0.0 for UK winter).
    // highlat_rule: Fallback rule if Fajr/Isha angles are unreachable.
    static PrayerTimes calculate(
        const Date& date,
        const Coordinates& coords,
        double timezone_offset,
        const Method& method,
        HighLatitudeRule highlat_rule = HighLatitudeRule::AUTO
    );

private:
    struct SolarPosition {
        double declination;
        double equation;
    };

    // Helper astronomical functions will be implemented in the .cpp file.
    static double julian_date(int year, int month, int day);
    static SolarPosition sun_position(double jd);
    static double mid_day(double jd);
    static double sun_angle_time(double angle, double time, double lat, double decl, double direction);
    static double asr_time(double time, double lat, double decl, double shadow_factor);
};

} // namespace prayertimes
