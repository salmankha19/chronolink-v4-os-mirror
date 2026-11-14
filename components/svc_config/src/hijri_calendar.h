// hijri_calendar.h
//
// ChronoLink V4 OS -- Algorithmic Gregorian-to-Hijri conversion.
//
// Pure C, no ESP-IDF dependency, no allocation, no logging. This file
// compiles with plain `gcc -std=gnu11 -c` for host tests.
//
// Uses the Kuwaiti algorithm (tabular Islamic calendar) for the base
// conversion. A user-configurable offset (applied in days) handles the
// ±1-2 day variance between this math and local mosque announcements.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;   // e.g., 1447
    int month;  // 1..12 (1 = Muharram, 9 = Ramadan)
    int day;    // 1..30 (or 29)
} hijri_date_t;

// Convert Gregorian to Hijri. Returns the Hijri date.
hijri_date_t gregorian_to_hijri(int g_year, int g_month, int g_day);

// Convert Gregorian to Hijri, applying a user offset (-3..+3 days).
hijri_date_t gregorian_to_hijri_offset(int g_year, int g_month, int g_day, int8_t offset_days);

// True iff the adjusted date falls in the month of Ramadan (month 9).
bool hijri_is_ramadan(int g_year, int g_month, int g_day, int8_t offset_days);

#ifdef __cplusplus
}
#endif
