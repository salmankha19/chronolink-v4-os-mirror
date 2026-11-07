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

    /* A single late tick (busy device) fires only what is still inside the
       lateness window: isha at 20:00 is 60 s old, while the four earlier
       prayers are older than SCHEDULER_MAX_LATENESS_S and are treated as
       missed (see test_lateness_limit). */
    int n = scheduler_evaluate(at(20, 1), &today, &st, fired);
    CHECK(n == 1 && fired[0] == 4,
          "catch-up tick at 20:01 fires only the 20:00 prayer");
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
    CHECK(scheduler_evaluate(at(10, 0), &old_day, &st, fired) == 1 &&
          fired[0] == 0, "fajr fires at 10:00");
    int n = scheduler_evaluate(at(12, 0), &old_day, &st, fired);
    CHECK(n == 1 && fired[0] == 1, "fajr + dhuhr fire before the config change");
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

    /* Minute sweep rather than one late tick: the lateness cap would treat a
       single 23:00 tick's 10:00 / 12:00 / 18:00 prayers as missed. */
    memset(&st, 0, sizeof st);
    int n = 0;
    for (int64_t now = at(9, 59); now <= at(21, 0); now += 60) {
        uint8_t ids[5];
        int k = scheduler_evaluate(now, &today, &st, ids);
        for (int j = 0; j < k && n < 5; j++) {
            fired[n] = ids[j];
            n++;
        }
    }
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

static void test_lateness_limit(void)
{
    scheduler_day_t today = make_day(0, true, 36000, 43200, 54000, 64800, 72000);
    scheduler_state_t st;
    uint8_t fired[5];

    /* Tick 3 minutes late: only the earliest prayer (10:00) is >2 min old,
       so it is skipped; nothing fires. */
    memset(&st, 0, sizeof st);
    CHECK(scheduler_evaluate(at(10, 3), &today, &st, fired) == 0,
          "3-min-late tick skips the missed prayer, fires nothing else");
    CHECK(st.last_fired_epoch[0] == at(10, 0),
          "missed prayer is marked fired, not retried");

    /* Tick 30s late: fires normally. */
    memset(&st, 0, sizeof st);
    int n = scheduler_evaluate(at(10, 0) + 30, &today, &st, fired);
    CHECK(n == 1 && fired[0] == 0, "30s late still fires");
}

static void test_isha_rollover(void)
{
    /* Day 1 Isha at 25:00 local (= 01:00 Day 2 UTC).
       Day 2 Isha at 20:00 local. Simulates svc_scheduler.c preserving Day 1's
       past-midnight Isha into s_state.pending_isha_epoch before clearing
       state at the Day 1 -> Day 2 rollover. */
    const int64_t day1_isha_utc = kMidnightUtc + 90000;          /* 01:00 Day 2 */
    const int64_t day2_isha_utc = kMidnightUtc + 86400 + 72000;  /* 20:00 Day 2 */

    scheduler_day_t day2 = make_day(0, true,
                                    36000, 43200, 54000, 64800, 72000);
    day2.date_yyyymmdd = kDate + 1;   /* 20260622 */

    scheduler_state_t st;
    uint8_t fired[SCHEDULER_PRAYER_COUNT];

    /* 30 min early: nothing fires, pending retained. */
    memset(&st, 0, sizeof st);
    st.pending_isha_epoch = day1_isha_utc;
    int n = scheduler_evaluate(day1_isha_utc - 1800, &day2, &st, fired);
    CHECK(n == 0, "pending isha does not fire 30 min early");
    CHECK(st.pending_isha_epoch == day1_isha_utc,
          "pending isha stays pending before its epoch");

    /* At its epoch: fires once, id 4, pending cleared, last_fired set. */
    memset(&st, 0, sizeof st);
    st.pending_isha_epoch = day1_isha_utc;
    n = scheduler_evaluate(day1_isha_utc + 1, &day2, &st, fired);
    CHECK(n == 1 && fired[0] == 4, "pending isha fires once as id 4");
    CHECK(st.last_fired_epoch[4] == day1_isha_utc,
          "pending isha epoch recorded in last_fired[4]");
    CHECK(st.pending_isha_epoch == 0, "pending isha consumed after firing");

    /* Second tick at the same time: nothing fires again. */
    n = scheduler_evaluate(day1_isha_utc + 1, &day2, &st, fired);
    CHECK(n == 0, "pending isha does not double-fire");

    /* Day 2's own Isha later that day: fires normally. (The four earlier
       prayers are >120 s past by now, so they are marked missed, not fired;
       only isha lands inside the lateness window.) */
    n = scheduler_evaluate(day2_isha_utc, &day2, &st, fired);
    CHECK(n == 1 && fired[0] == 4,
          "day 2's own isha fires after the rollover");

    /* Beyond the lateness cap: marked missed, not published. */
    memset(&st, 0, sizeof st);
    st.pending_isha_epoch = day1_isha_utc;
    n = scheduler_evaluate(day1_isha_utc + 180, &day2, &st, fired);
    CHECK(n == 0, "pending isha past the lateness cap is not fired");
    CHECK(st.last_fired_epoch[4] == day1_isha_utc,
          "pending isha past the lateness cap is marked fired");
    CHECK(st.pending_isha_epoch == 0, "missed pending isha is consumed");

    /* Premark grace: 1 s old, still inside the window, retained. */
    memset(&st, 0, sizeof st);
    st.pending_isha_epoch = day1_isha_utc;
    scheduler_premark_past(day1_isha_utc + 1, &day2, &st);
    CHECK(st.pending_isha_epoch == day1_isha_utc,
          "pending isha in the premark grace window stays pending");

    /* Premark past: 3 s old, outside the window, marked missed + cleared. */
    scheduler_premark_past(day1_isha_utc + 3, &day2, &st);
    CHECK(st.last_fired_epoch[4] == day1_isha_utc,
          "pending isha past the premark window is premarked");
    CHECK(st.pending_isha_epoch == 0,
          "premarked pending isha is cleared");
}

static void test_rearm_no_double_fire(void)
{
    scheduler_day_t today = make_day(0, true, 36000, 43200, 54000, 64800, 72000);
    scheduler_state_t st, rebuilt;
    uint8_t fired[5];

    /* Dhuhr fires on the 1 Hz tick; one second later a recalculation event
       rebuilds the state. The rebuilt state must not fire it again. */
    memset(&st, 0, sizeof st);
    CHECK(scheduler_evaluate(at(12, 0), &today, &st, fired) == 1 && fired[0] == 1,
          "rearm setup: Dhuhr fires");
    scheduler_rearm(at(12, 0) + 1, &today, &st, 0, &rebuilt);
    CHECK(scheduler_evaluate(at(12, 0) + 1, &today, &rebuilt, fired) == 0,
          "rearm 1 s after a fire does not double-fire");

    /* 3 s later the premark itself covers it (same outcome). */
    scheduler_rearm(at(12, 0) + 3, &today, &st, 0, &rebuilt);
    CHECK(scheduler_evaluate(at(12, 0) + 3, &today, &rebuilt, fired) == 0,
          "rearm 3 s after a fire does not double-fire");

    /* A prayer due in the very second of the rearm is still swallowed by
       nothing: it was never fired, so it fires. */
    memset(&st, 0, sizeof st);
    scheduler_rearm(at(10, 0), &today, &st, 0, &rebuilt);
    CHECK(scheduler_evaluate(at(10, 0), &today, &rebuilt, fired) == 1 && fired[0] == 0,
          "rearm in the prayer's own second still lets it fire");

    /* In-place rebuild (prev aliases out), the way svc_scheduler.c uses it. */
    memset(&st, 0, sizeof st);
    scheduler_evaluate(at(12, 0), &today, &st, fired);
    scheduler_rearm(at(12, 0) + 1, &today, &st, 0, &st);
    CHECK(scheduler_evaluate(at(12, 0) + 1, &today, &st, fired) == 0,
          "in-place rearm keeps the fired marker");

    /* Backwards NTP step: Asr fired 'early' on a wrong clock, then the clock
       is corrected one hour back. The marker is for a FUTURE instant, so it
       is dropped and Asr fires again at its true time. */
    memset(&st, 0, sizeof st);
    scheduler_evaluate(at(15, 0), &today, &st, fired);                /* early Asr */
    scheduler_rearm(at(14, 0), &today, &st, 0, &rebuilt);              /* clock now 14:00 */
    CHECK(scheduler_evaluate(at(15, 0), &today, &rebuilt, fired) == 1 && fired[0] == 2,
          "marker for a future instant is dropped: Asr fires at its true time");

    /* A prayer moved LATER after it fired (config change) still fires once more. */
    scheduler_day_t moved = make_day(0, true, 36000, 43200 + 300, 54000, 64800, 72000);
    memset(&st, 0, sizeof st);
    scheduler_evaluate(at(12, 0), &today, &st, fired);
    scheduler_rearm(at(12, 0) + 10, &moved, &st, 0, &rebuilt);
    CHECK(scheduler_evaluate(at(12, 5), &moved, &rebuilt, fired) == 1 && fired[0] == 1,
          "prayer moved later fires at its new instant");

    /* Pending Isha survives the rebuild; NULL prev is accepted. */
    scheduler_rearm(at(12, 0), &today, NULL, at(1, 0) + 86400, &rebuilt);
    CHECK(rebuilt.pending_isha_epoch == at(1, 0) + 86400, "pending Isha preserved by rearm");
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
    test_lateness_limit();
    test_isha_rollover();
    test_rearm_no_double_fire();
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
