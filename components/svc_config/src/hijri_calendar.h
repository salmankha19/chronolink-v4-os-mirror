// hijri_calendar.h
//
// ChronoLink V4 OS -- Umm Al-Qura Ramadan window lookup.
//
// INTERNAL to svc_prayer_engine_service (src/ is a PRIV_INCLUDE_DIRS path).
// Not part of the published API.
//
// Answers one question: for a given Gregorian date, is it inside a
// confirmed Ramadan window (the Saudi Umm al-Qura announcement)? That
// answer gates the engine's +120 Isha interval override for the
// UMM_AL_QURA method (PrayerTimesCalculator::calculate with is_ramadan).
//
// Scope of this revision:
//   - Confirmed windows for 2026 (1447 AH) and 2027 (1448 AH) are in
//     the static table inside the .c, sourced from the Saudi Supreme
//     Court announcements.
//   - Any Gregorian year NOT in the table returns false from
//     uaq_is_ramadan(), i.e. the safe non-Ramadan (+90 min Isha)
//     default. Users who want to force a specific window for an
//     unlisted year use the config override (svc_config ramadan_windows).
//   - Tabular Umm al-Qura prediction for other years is deferred to
//     the forthcoming Islamic calendar PR. The Umm al-Qura calendar is
//     a published table, not a purely arithmetic calendar; a naive
//     approximation drifts 2-3 days from the real window, so no
//     approximation ships in this revision.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t gregorian_year;  // year the window STARTS in
    uint8_t  start_month;     // 1..12
    uint8_t  start_day;       // 1..31
    uint8_t  end_month;       // 1..12; may be < start_month for a cross-year window
    uint8_t  end_day;         // 1..31
} uaq_ramadan_window_t;

// Look up a confirmed window. Returns true and fills *out when the given
// Gregorian year is in the table; returns false (out untouched) otherwise.
// Defensive against NULL out.
bool uaq_ramadan_confirmed_window(int gregorian_year, uaq_ramadan_window_t *out);

// True iff (gregorian_year, month, day) falls inside ANY confirmed window
// (this year's, or last year's spillover if that window crosses into
// January). Returns false for invalid dates, for years outside
// [1900, 2100], and for years not in the table.
// Deliberately narrow: no override windows here. The service layers its
// user overrides on top of this function's result, not inside it.
bool uaq_is_ramadan(int gregorian_year, int month, int day);

#ifdef __cplusplus
}
#endif
