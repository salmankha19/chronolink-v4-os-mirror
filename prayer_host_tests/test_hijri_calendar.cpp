// Host test for the algorithmic Hijri calendar (Kuwaiti tabular + Umm al-Qura).
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

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

static void test_uaq_method(void)
{
    // 2026-02-18 is 1 Ramadan 1447 in Umm al-Qura (KSA)
    CHECK(hijri_is_ramadan_method(2026, 2, 18, 0, 1), "2026-02-18 +0 UAQ is Ramadan");
    CHECK(hijri_is_ramadan_method(2026, 3, 19, 0, 1), "2026-03-19 +0 UAQ is Ramadan");
    CHECK(!hijri_is_ramadan_method(2026, 3, 20, 0, 1), "2026-03-20 +0 UAQ not Ramadan");

    // 2027-02-08 is 1 Ramadan 1448 in Umm al-Qura
    CHECK(hijri_is_ramadan_method(2027, 2, 8, 0, 1), "2027-02-08 +0 UAQ is Ramadan");
    CHECK(hijri_is_ramadan_method(2027, 3, 8, 0, 1), "2027-03-08 +0 UAQ is Ramadan");
    CHECK(!hijri_is_ramadan_method(2027, 3, 9, 0, 1), "2027-03-09 +0 UAQ not Ramadan"); // 29 days in UAQ

    // Verify tabular vs UAQ difference
    // Note: Tabular and UAQ agree on 2026-02-18 (1 Ramadan 1447).
    // The divergence happens on 2027-03-09: Tabular says 30 Ramadan,
    // UAQ says 1 Shawwal.
    CHECK(hijri_is_ramadan_method(2027, 3, 9, 0, 0), "2027-03-09 Tabular is Ramadan (30 days)");
}

static void test_gregorian_to_hijri_method(void)
{
    // Method 0 (tabular) must match the existing gregorian_to_hijri().
    hijri_date_t a = gregorian_to_hijri(2026, 2, 18);
    hijri_date_t b = gregorian_to_hijri_method(2026, 2, 18, 0, 0);
    CHECK(a.year == b.year && a.month == b.month && a.day == b.day,
          "method=0 matches gregorian_to_hijri()");

    // Method 1 (UAQ) on a confirmed-KSA date.
    b = gregorian_to_hijri_method(2026, 2, 18, 0, 1);
    CHECK(b.year == 1447 && b.month == 9 && b.day == 1,
          "method=1 (UAQ) on 2026-02-18 == 1 Ramadan 1447");

    // Offset applies on both methods.
    b = gregorian_to_hijri_method(2026, 2, 17, 1, 1);
    CHECK(b.year == 1447 && b.month == 9 && b.day == 1,
          "method=1 + offset +1 shifts 2026-02-17 into Ramadan");

    // Pre-1924 fallback regression: UAQ and tabular must agree on dates
    // below the UAQ table start (bug #1a was returning jd-1 here).
    a = gregorian_to_hijri(1920, 1, 1);
    b = gregorian_to_hijri_method(1920, 1, 1, 0, 1);
    CHECK(a.year == b.year && a.month == b.month && a.day == b.day,
          "pre-1924 UAQ fallback matches tabular (was returning jd-1)");

    // Post-table fallback: the UAQ table ends at 1500 AH (~2077 CE). For
    // later years the UAQ path falls back to tabular; without the jd + 1
    // fix above, it returned one day earlier than tabular. Reachable
    // through the firmware's [2020, 2100] year guard.
    a = gregorian_to_hijri(2090, 1, 1);
    b = gregorian_to_hijri_method(2090, 1, 1, 0, 1);
    CHECK(a.year == b.year && a.month == b.month && a.day == b.day,
          "post-table (2090) UAQ fallback matches tabular (was returning jd-1)");

    // Month-name lookups.
    CHECK(hijri_month_name(5) && strcmp(hijri_month_name(5), "Jumada al-Ula") == 0,
          "full name for month 5");
    CHECK(hijri_month_name_short(5) && strcmp(hijri_month_name_short(5), "Jumada I") == 0,
          "short name for month 5");
    CHECK(hijri_month_name(0) == NULL, "month 0 full name is NULL");
    CHECK(hijri_month_name(13) == NULL, "month 13 full name is NULL");
    CHECK(hijri_month_name_short(0) == NULL, "month 0 short name is NULL");
    CHECK(hijri_month_name_short(13) == NULL, "month 13 short name is NULL");
}

int main()
{
    printf("=== hijri calendar (algorithmic) ===\n");
    test_known_dates();
    test_offset();
    test_is_ramadan();
    test_uaq_method();
    test_gregorian_to_hijri_method();
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
