// Host test for the UAQ Ramadan window lookup
// (components/svc_config/src/hijri_calendar.c).
//
// Why this exists: the +120 Isha interval that the UMM_AL_QURA method
// applies during Ramadan is only safe if the "is it Ramadan?" gate is
// correct at both edges (start of Ramadan, end of Ramadan) and returns
// the safe non-Ramadan (+90) value for every other date. An off-by-one
// here silently shifts Isha by 30 minutes for one day, or -- worse --
// flips the whole 30-day window by a day because a source date was
// miscopied into the table.
//
// What is NOT tested here:
//   - Tabular prediction for years outside the confirmed table. That
//     feature does not exist yet (deferred to the Islamic calendar PR);
//     every non-2026/2027 year is expected to return false.
//   - The engine's +120 application itself. That is exercised by
//     test_mosque_calendars.cpp once the UAQ Ramadan fixture is added
//     there.
//   - The cross-year window path. The 2026/2027 entries do not cross a
//     year boundary; the code path is present and follows the same
//     integer range check as the same-year path.
//
// Pure C host test: no ESP-IDF, no config, no allocation. g++ links it
// against hijri_calendar.o compiled as C (see the Makefile).

#include <stdio.h>
#include <stdbool.h>

extern "C" {
#include "hijri_calendar.h"
}

static int bad = 0;

#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s", msg); putchar(10); bad++; } } while (0)

static void test_confirmed_table(void)
{
    uaq_ramadan_window_t w;

    CHECK(uaq_ramadan_confirmed_window(2026, &w),
          "2026 is in the confirmed table");
    CHECK(w.gregorian_year == 2026 &&
          w.start_month == 2 && w.start_day == 18 &&
          w.end_month   == 3 && w.end_day   == 19,
          "2026 window is Feb 18 - Mar 19 (30 days)");

    CHECK(uaq_ramadan_confirmed_window(2027, &w),
          "2027 is in the confirmed table");
    CHECK(w.gregorian_year == 2027 &&
          w.start_month == 2 && w.start_day == 8 &&
          w.end_month   == 3 && w.end_day   == 8,
          "2027 window is Feb 8 - Mar 8 (29 days)");

    CHECK(!uaq_ramadan_confirmed_window(2025, &w),
          "2025 is not in the confirmed table");
    CHECK(!uaq_ramadan_confirmed_window(2028, &w),
          "2028 is not in the confirmed table");
    CHECK(!uaq_ramadan_confirmed_window(1800, &w),
          "1800 is out of the 1900..2100 range");
    CHECK(!uaq_ramadan_confirmed_window(2200, &w),
          "2200 is out of the 1900..2100 range");
    CHECK(!uaq_ramadan_confirmed_window(2026, nullptr),
          "NULL out is rejected");
}

static void test_2026_edges(void)
{
    CHECK(!uaq_is_ramadan(2026, 2, 17), "2026-02-17 is the day before Ramadan");
    CHECK( uaq_is_ramadan(2026, 2, 18), "2026-02-18 is the first day of Ramadan");
    CHECK( uaq_is_ramadan(2026, 2, 19), "2026-02-19 is day 2");
    CHECK( uaq_is_ramadan(2026, 3, 18), "2026-03-18 is the day before Eid");
    CHECK( uaq_is_ramadan(2026, 3, 19), "2026-03-19 is the last day of Ramadan");
    CHECK(!uaq_is_ramadan(2026, 3, 20), "2026-03-20 is Eid, not Ramadan");
    CHECK(!uaq_is_ramadan(2026, 1, 15), "2026-01-15 is not Ramadan");
    CHECK(!uaq_is_ramadan(2026, 6, 15), "2026-06-15 is not Ramadan");
    CHECK(!uaq_is_ramadan(2026, 12, 15), "2026-12-15 is not Ramadan");
}

static void test_2027_edges(void)
{
    CHECK(!uaq_is_ramadan(2027, 2, 7), "2027-02-07 is the day before Ramadan");
    CHECK( uaq_is_ramadan(2027, 2, 8), "2027-02-08 is the first day of Ramadan");
    CHECK( uaq_is_ramadan(2027, 2, 9), "2027-02-09 is day 2");
    CHECK( uaq_is_ramadan(2027, 3, 7), "2027-03-07 is the day before Eid");
    CHECK( uaq_is_ramadan(2027, 3, 8), "2027-03-08 is the last day of Ramadan");
    CHECK(!uaq_is_ramadan(2027, 3, 9), "2027-03-09 is Eid, not Ramadan");
}

static void test_unlisted_years(void)
{
    CHECK(!uaq_is_ramadan(2025, 3, 1), "2025 not in table -> false");
    CHECK(!uaq_is_ramadan(2025, 2, 18), "2025 wrong year -> false");
    CHECK(!uaq_is_ramadan(2028, 2, 1), "2028 not in table -> false");
    CHECK(!uaq_is_ramadan(2030, 1, 1), "2030 not in table -> false");
}

static void test_invalid_input(void)
{
    CHECK(!uaq_is_ramadan(2026, 0, 1), "month 0 is rejected");
    CHECK(!uaq_is_ramadan(2026, 13, 1), "month 13 is rejected");
    CHECK(!uaq_is_ramadan(2026, 2, 0), "day 0 is rejected");
    CHECK(!uaq_is_ramadan(2026, 2, 32), "day 32 is rejected");
    CHECK(!uaq_is_ramadan(1800, 2, 18), "year 1800 is out of range");
    CHECK(!uaq_is_ramadan(2200, 2, 18), "year 2200 is out of range");
}

static void test_full_sweep_2026(void)
{
    // Every day of Ramadan 1447 (Feb 18 - Mar 19, 2026) is true.
    static const int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int total_in = 0;
    for (int doy = 0; doy < 365; doy++) {
        int m = 0, d = doy;
        while (d >= md[m]) { d -= md[m]; m++; }
        if (uaq_is_ramadan(2026, m + 1, d + 1)) total_in++;
    }
    CHECK(total_in == 30, "2026 Ramadan has exactly 30 days");
}

static void test_full_sweep_2027(void)
{
    // Every day of Ramadan 1448 (Feb 8 - Mar 8, 2027) is true.
    static const int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int total_in = 0;
    for (int doy = 0; doy < 365; doy++) {
        int m = 0, d = doy;
        while (d >= md[m]) { d -= md[m]; m++; }
        if (uaq_is_ramadan(2027, m + 1, d + 1)) total_in++;
    }
    CHECK(total_in == 29, "2027 Ramadan has exactly 29 days");
}

int main()
{
    printf("=== UAQ Ramadan windows (confirmed table) ===\n");
    test_confirmed_table();
    test_2026_edges();
    test_2027_edges();
    test_unlisted_years();
    test_invalid_input();
    test_full_sweep_2026();
    test_full_sweep_2027();
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
