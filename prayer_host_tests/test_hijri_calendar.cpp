// Host test for the algorithmic Hijri calendar (Kuwaiti algorithm).
#include <stdio.h>
#include <stdbool.h>

extern "C" {
#include "hijri_calendar.h"
}

static int bad = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); bad++; } } while (0)

static void test_known_dates(void)
{
    // 1 Ramadan 1447 AH = Feb 18, 2026
    hijri_date_t h = gregorian_to_hijri(2026, 2, 18);
    CHECK(h.year == 1447 && h.month == 9 && h.day == 1, "2026-02-18 is 1 Ramadan 1447");

    // 1 Ramadan 1448 AH = Feb 8, 2027 (approx in tabular, might be 1 day off, check it's close)
    h = gregorian_to_hijri(2027, 2, 8);
    CHECK(h.year == 1448 && h.month == 9, "2027-02-08 is in Ramadan 1448");

    // 1 Muharram 1447 AH = Jun 27, 2025 in the tabular calendar this
    // implementation uses (Umm al-Qura has Jun 26; a +/-1 day difference
    // is exactly what the user offset exists for). Jun 26 is the last day
    // of 1446 by the same math, so both edges get pinned.
    h = gregorian_to_hijri(2025, 6, 26);
    CHECK(h.year == 1446 && h.month == 12 && h.day == 29, "2025-06-26 is 29 Dhul-Hijjah 1446");
    h = gregorian_to_hijri(2025, 6, 27);
    CHECK(h.year == 1447 && h.month == 1 && h.day == 1, "2025-06-27 is 1 Muharram 1447");
}

static void test_offset(void)
{
    // Base date: Feb 17, 2026 (1 day before Ramadan starts in tabular)
    hijri_date_t base = gregorian_to_hijri(2026, 2, 17);
    CHECK(base.month == 8, "2026-02-17 is Sha'ban");

    // Add +1 offset -> should be 1 Ramadan
    hijri_date_t shifted = gregorian_to_hijri_offset(2026, 2, 17, 1);
    CHECK(shifted.month == 9 && shifted.day == 1, "2026-02-17 +1 day is 1 Ramadan");

    // With -1 offset, Feb 19 should be 1 Ramadan
    base = gregorian_to_hijri(2026, 2, 19);
    CHECK(base.month == 9 && base.day == 2, "2026-02-19 is 2 Ramadan");
    shifted = gregorian_to_hijri_offset(2026, 2, 19, -1);
    CHECK(shifted.month == 9 && shifted.day == 1, "2026-02-19 -1 day is 1 Ramadan");
}

static void test_is_ramadan(void)
{
    CHECK(!hijri_is_ramadan(2026, 2, 17, 0), "2026-02-17 +0 not Ramadan");
    CHECK(hijri_is_ramadan(2026, 2, 18, 0), "2026-02-18 +0 is Ramadan");
    CHECK(hijri_is_ramadan(2026, 3, 19, 0), "2026-03-19 +0 is Ramadan");
    CHECK(!hijri_is_ramadan(2026, 3, 20, 0), "2026-03-20 +0 not Ramadan");

    // Shift the window by +1
    CHECK(hijri_is_ramadan(2026, 2, 17, 1), "2026-02-17 +1 is Ramadan");
    CHECK(!hijri_is_ramadan(2026, 2, 18, -1), "2026-02-18 -1 not Ramadan (was 29 Sha'ban)");
}

int main()
{
    printf("=== hijri calendar (algorithmic) ===\n");
    test_known_dates();
    test_offset();
    test_is_ramadan();
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
