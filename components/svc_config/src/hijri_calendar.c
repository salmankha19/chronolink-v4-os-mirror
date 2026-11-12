// hijri_calendar.c
//
// ChronoLink V4 OS -- Umm Al-Qura Ramadan window lookup (implementation).
//
// See hijri_calendar.h for the contract and scope.
//
// Pure C, no ESP-IDF dependency, no allocation, no logging: this file
// must compile with plain `gcc -std=gnu11 -c` so it can be tested on
// the host without an ESP-IDF toolchain.

#include "hijri_calendar.h"

#include <stddef.h>

// Confirmed KSA (Umm al-Qura) Ramadan windows, sourced from the Saudi
// Supreme Court announcements. Add new years here when they are announced
// each year -- the rest of the pipeline picks them up automatically.
//
// Both 1447 and 1448 Ramadan windows are entirely within a single Gregorian
// year (start_month < end_month, no cross-year). A future entry that
// crosses a year boundary (end_month < start_month, e.g. a December to
// January window) is handled correctly by uaq_is_ramadan() below without
// any further change; the cross-year path is present but not exercised by
// the current table entries.
static const uaq_ramadan_window_t s_confirmed[] = {
    // Ramadan 1447 AH: Feb 18, 2026 - Mar 19, 2026 (30 days), Eid Mar 20, 2026
    { .gregorian_year = 2026, .start_month = 2, .start_day = 18,
                              .end_month   = 3, .end_day   = 19 },
    // Ramadan 1448 AH: Feb  8, 2027 - Mar  8, 2027 (29 days), Eid Mar  9, 2027
    { .gregorian_year = 2027, .start_month = 2, .start_day = 8,
                              .end_month   = 3, .end_day   = 8  },
};

#define CONFIRMED_COUNT (sizeof(s_confirmed) / sizeof(s_confirmed[0]))

bool uaq_ramadan_confirmed_window(int gregorian_year, uaq_ramadan_window_t *out)
{
    if (!out) return false;
    if (gregorian_year < 1900 || gregorian_year > 2100) return false;

    for (size_t i = 0; i < CONFIRMED_COUNT; i++) {
        if ((int)s_confirmed[i].gregorian_year == gregorian_year) {
            *out = s_confirmed[i];
            return true;
        }
    }
    return false;
}

// Convert (year, month, day) to a sortable integer yyyymmdd. Caller has
// already validated the ranges, so no defensive checks here.
static int yyyymmdd(int year, int month, int day)
{
    return year * 10000 + month * 100 + day;
}

// Check whether (query_year, query_month, query_day) falls inside the
// window that STARTS in start_year. Handles cross-year windows
// (end_month < start_month) by rolling the end forward one year.
static bool date_in_window(const uaq_ramadan_window_t *w, int start_year,
                           int query_year, int query_month, int query_day)
{
    int start = yyyymmdd(start_year, w->start_month, w->start_day);
    int end_year = (w->end_month < w->start_month) ? start_year + 1 : start_year;
    int end = yyyymmdd(end_year, w->end_month, w->end_day);
    int target = yyyymmdd(query_year, query_month, query_day);
    return target >= start && target <= end;
}

bool uaq_is_ramadan(int gregorian_year, int month, int day)
{
    if (gregorian_year < 1900 || gregorian_year > 2100) return false;
    if (month < 1 || month > 12) return false;
    if (day   < 1 || day   > 31) return false;

    uaq_ramadan_window_t w;

    // This year's window may itself cross into next year (start_year ==
    // query_year, end_year may be query_year + 1).
    if (uaq_ramadan_confirmed_window(gregorian_year, &w)) {
        if (date_in_window(&w, gregorian_year, gregorian_year, month, day)) {
            return true;
        }
    }

    // Last year's window may spill into January of this year.
    if (uaq_ramadan_confirmed_window(gregorian_year - 1, &w)) {
        if (date_in_window(&w, gregorian_year - 1, gregorian_year, month, day)) {
            return true;
        }
    }

    return false;
}
