/* Host test for the pure scheduler logic (components/svc_scheduler).
 *
 * No ESP-IDF, no hardware: the point of this test is the DST / NTP-jump
 * behaviour, which is impossible to rehearse on the device. Build:
 *   gcc -std=gnu11 -c scheduler_logic.c && g++ test_scheduler.cpp *.o
 * (see the test_scheduler target in the Makefile) */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "scheduler_logic.h"

static int bad = 0;

#define CHECK(cond, msg) do {                            \
        if (!(cond)) { printf("FAIL: %s\n", msg); bad++; } \
    } while (0)

/* 2026-06-21T00:00:00Z. Taken from the calendar, NOT from the code under
   test -- if scheduler_prayer_epoch() ever drifts, these tests notice. */
static const uint32_t kDate        = 20260621u;
static const int64_t  kMidnightUtc = 1782000000LL;

static int64_t at(int hour, int minute)
{
    return kMidnightUtc + (int64_t)hour * 3600 + (int64_t)minute * 60;
}

/* Fajr/Dhuhr/Asr/Maghrib/Isha in seconds since local midnight. */
static scheduler_day_t make_day(int16_t tz_minutes, bool valid,
                                uint32_t fajr, uint32_t dhuhr, uint32_t asr,
                                uint32_t maghrib, uint32_t isha)
{
    scheduler_day_t d;

    memset(&d, 0, sizeof d);
    d.date_yyyymmdd     = kDate;
    d.tz_offset_minutes = tz_minutes;
    d.valid             = valid;
    d.sec[0] = fajr;
    d.sec[1] = dhuhr;
    d.sec[2] = asr;
    d.sec[3] = maghrib;
    d.sec[4] = isha;
    return d;
}

static void test_epoch_conversion(void)
{
    /* offset 0: local seconds map straight onto the UTC day. */
    CHECK(scheduler_prayer_epoch(kDate, 0, 10u * 3600u) == at(10, 0),
          "offset 0: 10:00 local == 10:00 UTC");
    /* BST (+60): 10:00 local is 09:00 UTC. */
    CHECK(scheduler_prayer_epoch(kDate, 60, 10u * 3600u) == at(9, 0),
          "offset +60: 10:00 local == 09:00 UTC");
    /* West of Greenwich: 10:00 local is 15:00 UTC. */
    CHECK(scheduler_prayer_epoch(kDate, -300, 10u * 3600u) == at(15, 0),
          "offset -300: 10:00 local == 15:00 UTC");
    /* Isha / midnight past local 24:00 extend into the next day. */
    CHECK(scheduler_prayer_epoch(kDate, 0, 25u * 3600u) == at(1, 0) + 86400,
          "local seconds past 24:00 roll into the next UTC day");

    /* Sentinels and impossible dates convert to 0 (= not scheduled). */
    CHECK(scheduler_prayer_epoch(kDate, 0, UINT32_MAX) == 0,
          "UINT32_MAX is not a prayer instant");
    CHECK(scheduler_prayer_epoch(0u, 0, 3600u) == 0, "date 0 rejected");
    CHECK(scheduler_prayer_epoch(20261321u, 0, 3600u) == 0, "month 13 rejected");
    CHECK(scheduler_prayer_epoch(20260632u, 0, 3600u) == 0, "day 32 rejected");
    CHECK(scheduler_prayer_epoch(19690101u, 0, 3600u) == 0, "year < 1970 rejected");
}

static void test_happy_path(void)
{
    scheduler_day_t today = make_day(0, true, 36000, 43200, 54000, 64800, 72000);
    scheduler_state_t st;
    uint8_t fired[5];
    uint8_t seq[5] = { 0, 0, 0, 0, 0 };
    int total = 0;

    memset(&st, 0, sizeof st);

    /* A single late tick (busy device) catches up: five fires, in order. */
    int n = scheduler_evaluate(at(20, 1), &today, &st, fired);
    CHECK(n == 5, "catch-up tick at 20:01 fires exactly 5 prayers");
    for (int i = 0; i < n && i < 5; i++) {
        CHECK(fired[i] == (uint8_t)i, "catch-up fires in prayer order");
    }
    CHECK(st.last_fired_epoch[0] == at(10, 0) &&
          st.last_fired_epoch[4] == at(20, 0),
          "state stores the prayer epochs, not the tick epoch");
    CHECK(scheduler_evaluate(at(20, 1), &today, &st, fired) == 0,
          "a second tick at the same time fires nothing");

    /* Minute-by-minute 09:59 -> 20:01: exactly one fire per prayer. */
    memset(&st, 0, sizeof st);
    for (int64_t now = at(9, 59); now <= at(20, 1); now += 60) {
        uint8_t ids[5];
        int k = scheduler_evaluate(now, &today, &st, ids);
        for (int j = 0; j < k && total < 5; j++) {
            seq[total] = ids[j];
            total++;
        }
    }
    CHECK(total == 5, "minute-by-minute sweep fires exactly 5 prayers");
    for (int i = 0; i < 5; i++) {
        CHECK(seq[i] == (uint8_t)i, "sweep fires one prayer per instant, in order");
    }
}

static void test_dst_fall_back(void)
{
    scheduler_day_t today = make_day(0, true, 36000, 43200, 54000, 64800, 72000);
    scheduler_state_t st;
    uint8_t fired[5];
    const int64_t fajr_utc = scheduler_prayer_epoch(kDate, 0, 36000);

    memset(&st, 0, sizeof st);
    CHECK(fajr_utc == at(10, 0), "fajr epoch conversion");

    int n = scheduler_evaluate(fajr_utc, &today, &st, fired);
    CHECK(n == 1 && fired[0] == 0, "fajr fires at its epoch");

    /* Fall-back: the wall clock repeats the hour, so local 10:00 is seen
       twice. now goes backwards -> nothing fires; when 10:00 comes round
       again the epoch guard still holds. */
    CHECK(scheduler_evaluate(fajr_utc - 3600, &today, &st, fired) == 0,
          "nothing fires while the clock is rolled back");
    CHECK(scheduler_evaluate(fajr_utc, &today, &st, fired) == 0,
          "fajr does not re-fire when 10:00 repeats");
    CHECK(scheduler_evaluate(fajr_utc + 1, &today, &st, fired) == 0,
          "fajr does not re-fire just after the repeated hour");
    CHECK(scheduler_evaluate(at(11, 0), &today, &st, fired) == 0,
          "stepping to 11:00 does not re-fire the 10:00 prayer");

    n = scheduler_evaluate(at(12, 0), &today, &st, fired);
    CHECK(n == 1 && fired[0] == 1, "dhuhr fires once after the transition");
    CHECK(scheduler_evaluate(at(12, 0) + 1, &today, &st, fired) == 0,
          "dhuhr does not fire twice");
}

static void test_startup_mid_day(void)
{
    scheduler_day_t today = make_day(0, true, 36000, 43200, 54000, 64800, 72000);
    scheduler_state_t st;
    uint8_t fired[5];

    memset(&st, 0, sizeof st);
    scheduler_premark_past(at(13, 0), &today, &st);

    CHECK(st.last_fired_epoch[0] == at(10, 0), "10:00 pre-marked at a 13:00 boot");
    CHECK(st.last_fired_epoch[1] == at(12, 0), "12:00 pre-marked at a 13:00 boot");
    CHECK(st.last_fired_epoch[2] == 0 && st.last_fired_epoch[3] == 0 &&
          st.last_fired_epoch[4] == 0,
          "future prayers are not pre-marked");

    CHECK(scheduler_evaluate(at(13, 0), &today, &st, fired) == 0,
          "a 13:00 boot publishes nothing");
    CHECK(scheduler_evaluate(at(14, 59), &today, &st, fired) == 0,
          "no fire between 13:00 and 15:00");

    int n = scheduler_evaluate(at(15, 0), &today, &st, fired);
    CHECK(n == 1 && fired[0] == 2, "asr fires at 15:00");
    n = scheduler_evaluate(at(18, 0), &today, &st, fired);
    CHECK(n == 1 && fired[0] == 3, "maghrib fires at 18:00");
    n = scheduler_evaluate(at(20, 0), &today, &st, fired);
    CHECK(n == 1 && fired[0] == 4, "isha fires at 20:00");
    CHECK(scheduler_evaluate(at(23, 59), &today, &st, fired) == 0,
          "no extra fires for the rest of the day");
}

static void test_config_reset(void)
{
    /* Old times: Asr 15:00, Isha 20:00. Fire everything up to 13:00. */
    scheduler_day_t old_day = make_day(0, true, 36000, 43200, 54000, 64800, 72000);
    scheduler_state_t st;
    uint8_t fired[5];

    memset(&st, 0, sizeof st);
    int n = scheduler_evaluate(at(13, 0), &old_day, &st, fired);
    CHECK(n == 2, "fajr + dhuhr fire before the config change");
    CHECK(st.last_fired_epoch[0] == at(10, 0) &&
          st.last_fired_epoch[1] == at(12, 0),
          "pre-change epochs recorded");

    /* The user edits the config: Dhuhr moves to 13:30, Asr to 14:00, Isha
       to 19:00. The event handler resets the state and re-marks the past. */
    scheduler_day_t new_day = make_day(0, true, 36000, 48600, 50400, 64800, 68400);

    memset(&st, 0, sizeof st);
    scheduler_premark_past(at(13, 0), &new_day, &st);
    CHECK(st.last_fired_epoch[0] == at(10, 0),
          "unchanged past prayer stays pre-marked after the reset");
    CHECK(st.last_fired_epoch[1] == 0,
          "prayer moved into the future is not pre-marked");
    CHECK(scheduler_evaluate(at(13, 0), &new_day, &st, fired) == 0,
          "the reset itself publishes nothing");

    n = scheduler_evaluate(at(13, 30), &new_day, &st, fired);
    CHECK(n == 1 && fired[0] == 1, "moved dhuhr fires once at its new time");
    CHECK(scheduler_evaluate(at(13, 30) + 60, &new_day, &st, fired) == 0,
          "moved dhuhr does not fire twice");
    n = scheduler_evaluate(at(14, 0), &new_day, &st, fired);
    CHECK(n == 1 && fired[0] == 2, "moved asr fires once at its new time");
    n = scheduler_evaluate(at(18, 0), &new_day, &st, fired);
    CHECK(n == 1 && fired[0] == 3, "unchanged maghrib fires once at 18:00");
    n = scheduler_evaluate(at(19, 0), &new_day, &st, fired);
    CHECK(n == 1 && fired[0] == 4, "moved isha fires once at its new time");
    CHECK(scheduler_evaluate(at(19, 0) + 60, &new_day, &st, fired) == 0,
          "no repeat once the new times have fired");
}

static void test_unscheduled_and_invalid(void)
{
    /* Asr at local 0 and Isha unavailable (UINT32_MAX): only the three real
       instants fire. */
    scheduler_day_t today = make_day(0, true, 36000, 43200, 0, 64800, UINT32_MAX);
    scheduler_state_t st;
    uint8_t fired[5];

    memset(&st, 0, sizeof st);
    int n = scheduler_evaluate(at(23, 0), &today, &st, fired);
    CHECK(n == 3, "unscheduled prayers are skipped");
    CHECK(fired[0] == 0 && fired[1] == 1 && fired[2] == 3,
          "only scheduled prayers fire, in order");

    /* valid == false: nothing fires and nothing is pre-marked. */
    scheduler_day_t invalid = make_day(0, false, 36000, 43200, 54000, 64800, 72000);

    memset(&st, 0, sizeof st);
    CHECK(scheduler_evaluate(at(23, 0), &invalid, &st, fired) == 0,
          "invalid day fires nothing");
    scheduler_premark_past(at(23, 0), &invalid, &st);
    CHECK(st.last_fired_epoch[0] == 0, "invalid day pre-marks nothing");

    /* Defensive NULL handling: never crash a 1 Hz task. */
    CHECK(scheduler_evaluate(at(23, 0), NULL, &st, fired) == 0, "NULL day");
    CHECK(scheduler_evaluate(at(23, 0), &today, NULL, fired) == 0, "NULL state");
    CHECK(scheduler_evaluate(at(23, 0), &today, &st, NULL) == 0, "NULL output");
    CHECK(scheduler_prayer_epoch(0u, 0, 0u) == 0, "all-zero input is not a time");
    scheduler_premark_past(at(23, 0), NULL, &st);
    scheduler_premark_past(at(23, 0), &today, NULL);
    CHECK(1, "premark tolerates NULL");
}

static void test_premark_grace(void)
{
    scheduler_day_t today = make_day(0, true, 36000, 43200, 54000, 64800, 72000);
    scheduler_state_t st;
    uint8_t fired[5];

    /* A recalculation event processed in the very second Fajr is due must not
       swallow it: the tick that follows in the same loop iteration fires it. */
    memset(&st, 0, sizeof st);
    scheduler_premark_past(at(10, 0), &today, &st);
    CHECK(st.last_fired_epoch[0] == 0, "prayer due this second is not pre-marked");
    int n = scheduler_evaluate(at(10, 0), &today, &st, fired);
    CHECK(n == 1 && fired[0] == 0, "prayer due this second still fires after a premark");

    /* 1 s late is still inside the grace window; 3 s late is 'past'. */
    memset(&st, 0, sizeof st);
    scheduler_premark_past(at(10, 0) + 1, &today, &st);
    CHECK(st.last_fired_epoch[0] == 0, "1 s old prayer is inside the grace window");
    memset(&st, 0, sizeof st);
    scheduler_premark_past(at(10, 0) + 3, &today, &st);
    CHECK(st.last_fired_epoch[0] == at(10, 0), "3 s old prayer is pre-marked");
}

int main(void)
{
    printf("=== scheduler logic (epoch guard) ===\n");
    test_epoch_conversion();
    test_happy_path();
    test_dst_fall_back();
    test_startup_mid_day();
    test_config_reset();
    test_unscheduled_and_invalid();
    test_premark_grace();
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
