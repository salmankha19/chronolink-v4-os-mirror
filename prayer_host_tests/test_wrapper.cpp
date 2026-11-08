/* C-API test: units, unwrapped Isha/midnight, argument Validation. */
#include <stdlib.h>
#include "svc_prayer_engine.h"
#include <stdio.h>
#include <math.h>
static int bad = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); bad++; } } while (0)
int main() {
    svc_prayer_times_t t;
    // Shrewsbury, 21 June 2026, BST (+1), MWL, AUTO
    esp_err_t e = svc_prayer_engine_calculate(2026,6,21, 52.7075,-2.7527, 1.0, SVC_PRAYER_METHOD_MWL, SVC_PRAYER_MADHAB_STANDARD, SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t);
    CHECK(e == ESP_OK && t.valid, "Shrewsbury 21 Jun valid");
    printf("21 Jun Shrewsbury: fajr=%u sunrise=%u dhuhr=%u asr=%u maghrib=%u isha=%u midnight=%u\n",t.fajr_sec,t.sunrise_sec,t.dhuhr_sec,t.asr_sec,t.maghrib_sec,t.isha_sec,t.midnight_sec);
    CHECK(t.isha_sec > t.maghrib_sec, "isha_sec must be after maghrib_sec (no wrap to a small number)");
    CHECK(t.isha_sec < 172800, "isha_sec within 0..172799");
    CHECK(t.midnight_sec >= t.isha_sec, "midnight not before isha");
    CHECK(t.fajr_sec < t.sunrise_sec, "fajr before sunrise");

    /* #133 (A1): the SHARED astronomical instant gets two roundings --
       fajr_sec ceils (start-of-window, adhan must not fire early) and
       sehri_end_sec floors (end-of-window, user must not fast past the
       true boundary). Both come from the same raw value, so they
       bracket it with 0..60 s between them. */
    CHECK(t.sehri_end_sec <= t.fajr_sec,
          "sehri_end_sec (floor) <= fajr_sec (ceil) -- same instant, opposite roundings");
    CHECK((t.fajr_sec - t.sehri_end_sec) <= 60,
          "sehri_end_sec and fajr_sec bracket the same instant within 60 s");

    /* #133 (A1): every minute-rounded field must land on a 60-s boundary.
       Applies to both start-of-window (ceil) and end-of-window (floor)
       fields. asr_yellow_sec is guarded because it may hold the
       UINT32_MAX sentinel at high-latitude winter. */
    CHECK(t.fajr_sec % 60 == 0,     "fajr_sec minute-aligned");
    CHECK(t.sunrise_sec % 60 == 0,  "sunrise_sec minute-aligned");
    CHECK(t.dhuhr_sec % 60 == 0,    "dhuhr_sec minute-aligned");
    CHECK(t.asr_sec % 60 == 0,      "asr_sec minute-aligned");
    CHECK(t.maghrib_sec % 60 == 0,  "maghrib_sec minute-aligned");
    CHECK(t.isha_sec % 60 == 0,     "isha_sec minute-aligned");
    CHECK(t.midnight_sec % 60 == 0, "midnight_sec minute-aligned");
    CHECK(t.zawal_sec % 60 == 0,    "zawal_sec minute-aligned");
    CHECK(t.sehri_end_sec % 60 == 0, "sehri_end_sec minute-aligned");
    if (t.asr_yellow_sec != UINT32_MAX) {
        CHECK(t.asr_yellow_sec % 60 == 0,
              "asr_yellow_sec minute-aligned when not the UINT32_MAX sentinel");
    }

    /* #133 (A3): zawal_sec is the astronomical zawal (sun at/just past
       zenith). dhuhr_sec (start-of-window) is at or after it. If zawal
       were computed from dhuhr -- the A3 bug -- this ordering would be
       inverted or equal-though-later for a daylight location. */
    CHECK(t.zawal_sec <= t.dhuhr_sec,
          "zawal_sec (astronomical zawal) <= dhuhr_sec (start-of-window)");
    CHECK(llabs((long long)t.fajr_sec + 86400 - (long long)t.isha_sec) > 1800, "Fajr and Isha are at least 30 min apart");
    // Hanafi Asr later than standard
    svc_prayer_times_t h; svc_prayer_engine_calculate(2026,6,21,52.7075,-2.7527,1.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_HANAFI,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &h);
    CHECK(h.asr_sec > t.asr_sec, "Hanafi Asr later than standard");
    // Validation
    CHECK(svc_prayer_engine_calculate(2026,6,21, 91.0,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t)==ESP_ERR_INVALID_ARG,"lat 91 rejected");
    CHECK(svc_prayer_engine_calculate(2026,6,21, NAN,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t)==ESP_ERR_INVALID_ARG,"NaN lat rejected");
    CHECK(svc_prayer_engine_calculate(2026,6,21, 52,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, nullptr)==ESP_ERR_INVALID_ARG,"NULL out rejected");
    // Polar day: Tromso, midsummer -> invalid state, zeroed output
    e = svc_prayer_engine_calculate(2026,6,21, 69.65,18.96,2.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t);
    CHECK(e == ESP_ERR_INVALID_STATE && !t.valid && t.fajr_sec == 0, "polar day -> INVALID_STATE");
    // --- date validation (31 Feb, 29 Feb non-leap) ---
    CHECK(svc_prayer_engine_calculate(2026,2,31,52,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t)==ESP_ERR_INVALID_ARG,"31 Feb rejected");
    CHECK(svc_prayer_engine_calculate(2026,2,29,52,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t)==ESP_ERR_INVALID_ARG,"29 Feb 2026 rejected");
    CHECK(svc_prayer_engine_calculate(2028,2,29,52,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t)==ESP_OK,"29 Feb 2028 accepted");
    // --- Asr-yellow sentinel: 62N midwinter, sun never reaches +5 deg ---
    e = svc_prayer_engine_calculate(2026,12,21,62.0,10.0,1.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t);
    CHECK(e==ESP_OK && t.valid && t.asr_yellow_sec==UINT32_MAX, "asr_yellow sentinel at 62N midwinter");
    // normal latitude: asr_yellow is a real time
    svc_prayer_engine_calculate(2026,10,17,52.7075,-2.7527,1.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t);
    CHECK(t.asr_yellow_sec!=UINT32_MAX && t.asr_yellow_sec>t.asr_sec && t.asr_yellow_sec<t.maghrib_sec, "asr_yellow between asr and maghrib");
    // --- Jafaria reachable ---
    e = svc_prayer_engine_calculate(2026,10,17,35.69,51.39,3.5,SVC_PRAYER_METHOD_JAFARIA,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t);
    CHECK(e==ESP_OK && t.valid, "Jafaria calculates");

    /* #138 (m): Moonsighting PRECONDITION guard. The Moonsighting method
       applies a +5 min adjustment to dhuhr (see test_moonsighting.cpp
       header: "Dhuhr (+5 min) and Maghrib (+3 min) ARE compared"). That
       adjustment is what makes #133 (A3) observable: A3 computed the
       zawal window start from raw.dhuhr_sec instead of raw.zawal_sec,
       pushing the safe-stop boundary ~5 min late. Without the +5 min
       adjustment, zawal and dhuhr would be within seconds and A3 would
       have been invisible.

       This assertion guards that PRECONDITION, not the A3 fix itself.
       A3 lives in svc_prayer_engine_service.c (fresh_zawal =
       raw.dhuhr_sec -> raw.zawal_sec), which this C-wrapper harness does
       NOT exercise. A true service-layer counterfactual test needs mocks
       for svc_config_get, svc_time_get_snapshot, svc_timetable_get_today,
       and adhan_calculator_run -- deferred to a future harness PR.

       Expected gap: 360 s, not 300 s. dhuhr is a start-of-window field
       (rounds UP to the next whole minute) and zawal is an end-of-window
       field (rounds DOWN to the previous whole minute), so the 300 s raw
       offset gains up to 60 s from each side; with the transit off a
       minute boundary the observed gap is exactly 360 s (300 s only if
       the transit lands exactly on a minute boundary). 240-360 brackets
       both, and sits on the inclusive upper bound by design. */
    e = svc_prayer_engine_calculate(2026,6,21, 52.7075,-2.7527, 1.0,
                                     SVC_PRAYER_METHOD_MOONSIGHTING,
                                     SVC_PRAYER_MADHAB_STANDARD,
                                     SVC_PRAYER_HIGHLAT_AUTO,
                                     0.0, 0.0, &t);
    CHECK(e==ESP_OK && t.valid, "Moonsighting calculates (Shrewsbury 21 Jun)");
    if (t.valid) {
        long long gap = (long long)t.dhuhr_sec - (long long)t.zawal_sec;
        CHECK(gap >= 240 && gap <= 360,
              "Moonsighting: dhuhr_sec - zawal_sec in [240, 360] s "
              "(method +5 min adjustment is the A3 precondition)");
    }
    // --- Shrewsbury across the year: isha always after maghrib, in range ---
    int wrap=0; for (int doy=0; doy<365; doy++) { static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31}; int m=0,d=doy; while(d>=md[m]){d-=md[m];m++;}
        svc_prayer_engine_calculate(2026,m+1,d+1,52.7075,-2.7527,0.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &t);
        if (!t.valid || t.isha_sec <= t.maghrib_sec || t.isha_sec >= 172800) wrap++; }
    CHECK(wrap==0, "Shrewsbury 2026: Isha after Maghrib on all 365 days");

    /* --- MASJID QUBA (London N16 6AA) DIAGNOSTIC v2 -- TEMPORARY ---
       Multi-config comparison at exact coordinates from Google Maps
       (HW8J+2G London: 51.5651073, -0.0687256). 13 dates spanning
       seasons + both DST transitions. All four configs use HANAFI
       madhab -- v1's Asr delta vs Standard confirmed Hanafi (Shafi'i
       Asr was 30-74 min earlier than the calendar across all seasons).
       Candidate methods: ISNA 15/15, MWL 18/17, Karachi 18/18,
       Moonsighting 18/18 + high-lat special. The v1 MWL/Standard run
       ruled out MWL as Fajr/Isha source (Fajr 23 min early, Isha 17
       min late in winter); this run answers "which method fits best"
       by exposing all four side by side.
       tz: 0 = GMT (winter), 1 = BST (Mar 29..Oct 25 2026). */
    {
        static const struct { int m, d, tz; const char *label; } q[] = {
            {1,  2, 0, "Jan 02 Fri"}, {1,  3, 0, "Jan 03 Sat"},
            {2,  2, 0, "Feb 02 Mon"}, {3,  2, 0, "Mar 02 Mon"},
            {3, 29, 1, "Mar 29 Sun"}, {4,  1, 1, "Apr 01 Wed"},
            {5,  1, 1, "May 01 Fri"}, {6, 21, 1, "Jun 21 Sun"},
            {7,  1, 1, "Jul 01 Wed"}, {8,  1, 1, "Aug 01 Sat"},
            {10,25, 0, "Oct 25 Sun"}, {11, 1, 0, "Nov 01 Sun"},
            {12,21, 0, "Dec 21 Mon"},
        };
        static const struct {
            svc_prayer_method_t m;
            const char *name;
        } methods[] = {
            { SVC_PRAYER_METHOD_ISNA,         "ISNA 15/15" },
            { SVC_PRAYER_METHOD_MWL,          "MWL 18/17"  },
            { SVC_PRAYER_METHOD_KARACHI,      "KHI 18/18"  },
            { SVC_PRAYER_METHOD_MOONSIGHTING, "MOON 18/18" },
        };
        auto pr = [](uint32_t s) {
            printf(" %2u:%02u", (unsigned)(s/3600u),
                               (unsigned)((s%3600u)/60u));
        };
        for (auto &mm : methods) {
            printf("\n=== MASJID QUBA 51.5651N, 0.0687W | %s | HANAFI AUTO ===\n",
                   mm.name);
            printf("%-11s %6s %6s %6s %6s %6s %6s %6s\n",
                   "date", "fajr", "sun", "istiw", "dhuhr", "asr", "magh", "isha");
            for (auto &dt : q) {
                svc_prayer_times_t r;
                esp_err_t e2 = svc_prayer_engine_calculate(
                    2026, dt.m, dt.d, 51.56510732559983, -0.0687256000863376, (double)dt.tz,
                    mm.m, SVC_PRAYER_MADHAB_HANAFI,
                    SVC_PRAYER_HIGHLAT_AUTO, 0.0, 0.0, &r);
                printf("%-11s", dt.label);
                if (e2 != ESP_OK || !r.valid) {
                    printf(" INVALID (e=%d)\n", (int)e2);
                    continue;
                }
                pr(r.fajr_sec); pr(r.sunrise_sec); pr(r.zawal_sec);
                pr(r.dhuhr_sec); pr(r.asr_sec);
                pr(r.maghrib_sec); pr(r.isha_sec);
                printf("\n");
            }
        }
        printf("=== end diagnostic ===\n");
    }

    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}