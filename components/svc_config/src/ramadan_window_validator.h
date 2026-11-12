// ramadan_window_validator.h
//
// ChronoLink V4 OS -- Ramadan override window validation.
//
// INTERNAL to svc_config (src/ is a private include path, not published
// API). Pure boolean check: is this (start_date, end_date) pair a coherent
// Gregorian window? Consumed by svc_config_set_ramadan_window() to reject
// user input before it reaches NVS.
//
// No ESP-IDF deps, no allocation, no logging. Compiles with plain
// `gcc -std=gnu11 -c` so it can be tested on the host.

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// True iff (start_year, start_month, start_day) and (end_year, end_month,
// end_day) form a coherent Gregorian window. Specifically:
//   - all six fields in valid Gregorian ranges (year 1900..2100, month
//     1..12, day 1..days_in_month(year, month), leap-year aware)
//   - window is either same-year (end_year == start_year and the
//     integer yyyymmdd(end) is >= yyyymmdd(start)) or single-year-cross
//     (end_year == start_year + 1, any valid dates)
//   - multi-year windows (end_year > start_year + 1) and inverted windows
//     (end_year < start_year) are rejected
// A single-day window (start == end) is accepted.
bool svc_config_ramadan_window_is_valid(int start_year, int start_month, int start_day,
                                        int end_year, int end_month, int end_day);

#ifdef __cplusplus
}
#endif
