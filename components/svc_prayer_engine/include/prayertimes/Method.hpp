/*
 * Method.hpp
 *
 * ChronoLink V4 OS -- Standalone Prayer Time Calculation Library
 *
 * Embedded C++ (no STL, no dynamic allocation, no exceptions).
 * This header defines the calculation methods (templates).
 */
#pragma once

namespace prayertimes {

struct Method {
    double fajr_angle;
    double isha_angle;
    int    isha_interval; // In minutes. If > 0, overrides isha_angle.
    double maghrib_angle;
    double midnight_angle;

    // Asr shadow factor: 1.0 for Standard (Shafi, Maliki, Hanbali),
    // 2.0 for Hanafi.
    double asr_shadow_factor;

    // True for Moonsighting Committee method (applies seasonal adjustments)
    bool is_moonsighting;
};

// --- Built-in Methods ---

constexpr Method MWL = {
    .fajr_angle = 18.0,
    .isha_angle = 17.0,
    .isha_interval = 0,
    .maghrib_angle = 0.0,
    .midnight_angle = 0.0,
    .asr_shadow_factor = 1.0,
    .is_moonsighting = false
};

constexpr Method ISNA = {
    .fajr_angle = 15.0,
    .isha_angle = 15.0,
    .isha_interval = 0,
    .maghrib_angle = 0.0,
    .midnight_angle = 0.0,
    .asr_shadow_factor = 1.0,
    .is_moonsighting = false
};

constexpr Method EGYPTIAN = {
    .fajr_angle = 19.5,
    .isha_angle = 17.5,
    .isha_interval = 0,
    .maghrib_angle = 0.0,
    .midnight_angle = 0.0,
    .asr_shadow_factor = 1.0,
    .is_moonsighting = false
};

constexpr Method KARACHI = {
    .fajr_angle = 18.0,
    .isha_angle = 18.0,
    .isha_interval = 0,
    .maghrib_angle = 0.0,
    .midnight_angle = 0.0,
    .asr_shadow_factor = 1.0,
    .is_moonsighting = false
};

constexpr Method UMM_AL_QURA = {
    .fajr_angle = 18.5,
    .isha_angle = 0.0,
    .isha_interval = 90, // 90 mins after Maghrib
    .maghrib_angle = 0.0,
    .midnight_angle = 0.0,
    .asr_shadow_factor = 1.0,
    .is_moonsighting = false
};

constexpr Method JAFARIA = {
    .fajr_angle = 16.0,
    .isha_angle = 14.0,
    .isha_interval = 0,
    .maghrib_angle = 4.0, // Maghrib 4 degrees
    .midnight_angle = 0.0,
    .asr_shadow_factor = 1.0,
    .is_moonsighting = false
};

constexpr Method MOONSIGHTING = {
    .fajr_angle = 18.0,
    .isha_angle = 18.0,
    .isha_interval = 0,
    .maghrib_angle = 0.0,
    .midnight_angle = 0.0,
    .asr_shadow_factor = 1.0,
    .is_moonsighting = true
};

} // namespace prayertimes
