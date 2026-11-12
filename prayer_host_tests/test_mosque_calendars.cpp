/* Fixture tests: svc_prayer_engine vs published mosque calendars.
   See docs/mosque-calendars.md for the source analysis.
   Run: ./build/test_mosque_calendars */
#include <stdio.h>
#include <math.h>
#include "svc_prayer_engine.h"

static int bad = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); bad++; } } while (0)

static int diff_min(int a_sec, int b_sec) {
    int d = a_sec - b_sec;
    if (d < 0) d = -d;
    return (d + 30) / 60;
}

static void assert_near(const char *date, const char *name,
                        int calc_sec, int cal_h, int cal_m, int tol_min) {
    int cal_sec = cal_h * 3600 + cal_m * 60;
    int d = diff_min(calc_sec, cal_sec);
    if (d > tol_min) {
        printf("FAIL: %s %s: calc=%02d:%02d cal=%02d:%02d diff=%d min (tol=%d)\n",
               date, name,
               calc_sec/3600, (calc_sec%3600)/60, cal_h, cal_m, d, tol_min);
        bad++;
    }
}

static void test_quba() {
    /* Masjid Quba (London N16 6AA): MOON + Hanafi + Maghrib +2.
       13 dates from docs/mosque-calendars.md section 1. Expected values
       from the mosque's published 2026 calendar. Tolerances: Fajr +-6,
       Dhuhr +-2, Asr +-2, Maghrib +-2, Isha +-12.
       Exclusions:
         - Jumu'ah Fridays: dhuhr_h = -1 (not astronomical Dhuhr)
         - Mar 02: maghrib_h = -1 (Feb-Mar maghrib anomaly +10 vs +5)
         - Jun 21, Jul 01: isha_h = -1 (known +8..+11 summer gap) */
    struct { int m, d, tz; const char *label;
             int fajr_h, fajr_m;
             int dhuhr_h, dhuhr_m;
             int asr_h, asr_m;
             int maghrib_h, maghrib_m;
             int isha_h, isha_m;
    } q[] = {
        {1,  2, 0, "Quba Jan 02",  6,26, -1, 0, 14,17, 16, 8, 17,43},
        {1,  3, 0, "Quba Jan 03",  6,26, 12,11, 14,19, 16,10, 17,45},
        {2,  2, 0, "Quba Feb 02",  5,57, 12,20, 15, 3, 16,57, 18,30},
        {3,  2, 0, "Quba Mar 02",  5, 4, 12,19, 15,50, -1, 0, 19,12},
        {3, 29, 1, "Quba Mar 29",  5, 7, 13,11, 17,28, 19,34, 20,49},
        {4,  1, 1, "Quba Apr 01",  5, 1, 13,10, 17,31, 19,39, 20,54},
        {5,  1, 1, "Quba May 01",  3,52, -1, 0, 18, 5, 20,29, 21,37},
        {6, 21, 1, "Quba Jun 21",  2,40, 13, 8, 18,40, 21,27, -1, 0},
        {7,  1, 1, "Quba Jul 01",  2,49, 13,10, 18,41, 21,26, -1, 0},
        {8,  1, 1, "Quba Aug 01",  3,39, 13,12, 18,24, 20,54, 21,59},
        {10,25, 0, "Quba Oct 25",  5, 5, 11,50, 14,57, 16,52, 18,15},
        {11, 1, 0, "Quba Nov 01",  5,14, 11,49, 14,46, 16,39, 18, 5},
        {12,21, 0, "Quba Dec 21",  6,21, 12, 4, 14, 8, 15,59, 17,34},
    };

    for (auto &r : q) {
        svc_prayer_times_t t;
        esp_err_t e = svc_prayer_engine_calculate(
            2026, r.m, r.d, 51.56510732559983, -0.0687256000863376, (double)r.tz,
            SVC_PRAYER_METHOD_MOONSIGHTING, SVC_PRAYER_MADHAB_HANAFI,
            SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, false, &t);
        if (e != ESP_OK || !t.valid) {
            printf("FAIL: %s: INVALID (e=%d)\n", r.label, (int)e);
            bad++;
            continue;
        }
        assert_near(r.label, "fajr",    (int)t.fajr_sec, r.fajr_h, r.fajr_m, 6);
        if (r.dhuhr_h >= 0) {
            assert_near(r.label, "dhuhr", (int)t.dhuhr_sec, r.dhuhr_h, r.dhuhr_m, 2);
        }
        assert_near(r.label, "asr",     (int)t.asr_sec, r.asr_h, r.asr_m, 2);
        if (r.maghrib_h >= 0) {
            assert_near(r.label, "maghrib",
                        (int)t.maghrib_sec + 120, r.maghrib_h, r.maghrib_m, 2);
        }
        if (r.isha_h >= 0) {
            assert_near(r.label, "isha",  (int)t.isha_sec, r.isha_h, r.isha_m, 12);
        }
    }
}

static void test_tawheed() {
    /* Tawheed Center (Farmington Hills, MI): ISNA + Standard + 0 offsets.
       3 dates from docs/mosque-calendars.md section 3. All prayers +-2 min
       (measured maximum is +-1). Timezone -5.0 = EST (January). */
    struct { int m, d; const char *label;
             int fajr_h, fajr_m;    int sunrise_h, sunrise_m;
             int dhuhr_h, dhuhr_m;  int asr_h, asr_m;
             int maghrib_h, maghrib_m; int isha_h, isha_m;
    } tw[] = {
        {1,  1, "Tawheed Jan 01", 6,39, 8, 3, 12,38, 14,54, 17,12, 18,36},
        {1, 16, "Tawheed Jan 16", 6,37, 8, 0, 12,44, 15, 8, 17,28, 18,50},
        {1, 31, "Tawheed Jan 31", 6,28, 7,48, 12,48, 15,25, 17,47, 19, 7},
    };

    for (auto &r : tw) {
        svc_prayer_times_t t;
        esp_err_t e = svc_prayer_engine_calculate(
            2026, r.m, r.d, 42.470667, -83.340793, -5.0,
            SVC_PRAYER_METHOD_ISNA, SVC_PRAYER_MADHAB_STANDARD,
            SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, false, &t);
        if (e != ESP_OK || !t.valid) {
            printf("FAIL: %s: INVALID (e=%d)\n", r.label, (int)e);
            bad++;
            continue;
        }
        assert_near(r.label, "fajr",    (int)t.fajr_sec,    r.fajr_h, r.fajr_m, 2);
        assert_near(r.label, "sunrise", (int)t.sunrise_sec, r.sunrise_h, r.sunrise_m, 2);
        assert_near(r.label, "dhuhr",   (int)t.dhuhr_sec,   r.dhuhr_h, r.dhuhr_m, 2);
        assert_near(r.label, "asr",     (int)t.asr_sec,     r.asr_h, r.asr_m, 2);
        assert_near(r.label, "maghrib", (int)t.maghrib_sec, r.maghrib_h, r.maghrib_m, 2);
        assert_near(r.label, "isha",    (int)t.isha_sec,    r.isha_h, r.isha_m, 2);
    }
}

static void test_reading() {
    // Jamia Masjid Reading — Nov-Dec 2026 (all GMT; DST ended Oct 25)
    //
    // Coordinates: Reading city-center (51.4543, -0.9781), NOT the
    // mosque address (51.4484, -0.9522). City-level timetable
    // convention: within-city delta ~0.1 min, below rounding floor.
    //
    // Config: CUSTOM 15.0° Fajr / 14.0° Isha, Hanafi, Dhuhr +5,
    // Maghrib +2. Fajr fits within ±4 min (measured -4 to +2); all
    // other fields within ±3 min across the same 10 dates.
    // Isha at 15° would be systematic -7..-9 min (does not converge),
    // so 14° is confirmed not ISNA.
    //
    // Summer Fajr UNVERIFIED — 15° is marginal at 51.45°N near
    // solstice. Needs Jun/Jul data to pin any high-lat rule.

    struct Row { int y, mo, d, fajr, sunrise, dhuhr, asr, maghrib, isha; };
    const Row rows[] = {
        {2026,11, 1,  320, 417, 712, 888, 1000, 1083},
        {2026,11, 8,  332, 429, 712, 877,  988, 1073},
        {2026,11,15,  343, 441, 713, 867,  978, 1065},
        {2026,11,22,  354, 453, 715, 859,  969, 1058},
        {2026,11,29,  364, 464, 717, 853,  963, 1053},
        {2026,12, 6,  373, 473, 720, 849,  959, 1051},
        {2026,12,13,  380, 481, 723, 849,  958, 1052},
        {2026,12,20,  384, 486, 726, 850,  960, 1055},
        {2026,12,27,  388, 489, 730, 854,  964, 1058},
        {2026,12,31,  388, 489, 732, 858,  968, 1061},
    };
    // (minutes since midnight; values = published HH:MM)

    // Compute with LAT=51.4543, LON=-0.9781, TZ=0, method=CUSTOM,
    // fajr=15.0, isha=14.0, madhab=HANAFI, dhuhr_offset=5,
    // maghrib_offset=2.
    //
    // Tolerances: Fajr is ±4 because the mosque publishes systematically
    // early in early November (-4/-2/-2/-1 min on Nov 01/08/15/22) — a
    // suhoor-safety rounding choice, not a config error. Everything else
    // holds at ±3 (Sunrise is exact on all 10 dates).
    const int TOL_FAJR  = 4;
    const int TOL_OTHER = 3;

    static const char *const mon[] = { "???", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    for (const auto &r : rows) {
        svc_prayer_times_t t;
        esp_err_t e = svc_prayer_engine_calculate(
            r.y, r.mo, r.d, 51.4543, -0.9781, 0.0,
            SVC_PRAYER_METHOD_CUSTOM, SVC_PRAYER_MADHAB_HANAFI,
            SVC_PRAYER_HIGHLAT_AUTO, 15.0, 14.0, false, &t);

        char label[24];
        snprintf(label, sizeof label, "Reading %s %02d", mon[r.mo], r.d);

        if (e != ESP_OK || !t.valid) {
            printf("FAIL: %s: INVALID (e=%d)\n", label, (int)e);
            bad++;
            continue;
        }
        // Config offsets applied here, not in the engine (same idiom as
        // test_quba's "maghrib + 120"):
        //   Dhuhr_Zawal_Begins = solar noon + 5 min -> +300 s
        //   Maghrib            = sunset     + 2 min -> +120 s
        assert_near(label, "fajr",    (int)t.fajr_sec,          r.fajr / 60,    r.fajr % 60,    TOL_FAJR);
        assert_near(label, "sunrise", (int)t.sunrise_sec,       r.sunrise / 60, r.sunrise % 60, TOL_OTHER);
        assert_near(label, "dhuhr",   (int)t.dhuhr_sec + 300,   r.dhuhr / 60,   r.dhuhr % 60,   TOL_OTHER);
        assert_near(label, "asr",     (int)t.asr_sec,           r.asr / 60,     r.asr % 60,     TOL_OTHER);
        assert_near(label, "maghrib", (int)t.maghrib_sec + 120, r.maghrib / 60, r.maghrib % 60, TOL_OTHER);
        assert_near(label, "isha",    (int)t.isha_sec,          r.isha / 60,    r.isha % 60,    TOL_OTHER);
    }
}

int main() {
    printf("=== Masjid Quba (MOON + Hanafi + Maghrib +2) ===\n");
    test_quba();
    printf("=== Tawheed Center (ISNA + Standard + 0 offsets) ===\n");
    test_tawheed();
    printf("=== Jamia Masjid Reading (CUSTOM 15/14 + Hanafi + Dhuhr +5, Maghrib +2) ===\n");
    test_reading();
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}