/*
 * scheduler_logic.c
 *
 * ChronoLink V4 OS -- prayer scheduler decision logic (implementation).
 *
 * Pure C by design: no ESP-IDF includes, no FreeRTOS, no allocation. Must
 * build with plain `gcc -std=gnu11 -c`. See scheduler_logic.h for the
 * epoch-guard invariant.
 */
#include "scheduler_logic.h"

#include <string.h>
#include <time.h>

/* timegm() is the inverse of gmtime_r(): it reads a struct tm as UTC. It is
   not part of ISO C, and Windows' CRT spells it _mkgmtime(). */
#ifdef _WIN32
#define SCHEDULER_TIMEGM _mkgmtime
#else
#define SCHEDULER_TIMEGM timegm
#endif

/* A prayer is scheduled when its seconds-since-local-midnight value is a
   real instant. 0 is local midnight (and what a zeroed struct holds);
   UINT32_MAX is the engine's "cannot be computed / disabled" sentinel. */
static bool prayer_scheduled(const scheduler_day_t *today, int p)
{
    if (!today || !today->valid) {
        return false;
    }
    if (p < 0 || p >= SCHEDULER_PRAYER_COUNT) {
        return false;
    }
    return today->sec[p] != 0u && today->sec[p] != UINT32_MAX;
}

int64_t scheduler_prayer_epoch(uint32_t date_yyyymmdd,
                               int16_t tz_offset_minutes,
                               uint32_t local_sec)
{
    struct tm midnight;
    time_t midnight_utc;
    int year;
    int month;
    int day;

    if (date_yyyymmdd == 0u || local_sec == UINT32_MAX) {
        return 0;
    }

    year  = (int)(date_yyyymmdd / 10000u);
    month = (int)((date_yyyymmdd / 100u) % 100u);
    day   = (int)(date_yyyymmdd % 100u);
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) {
        return 0;
    }

    memset(&midnight, 0, sizeof(midnight));
    midnight.tm_year  = year - 1900;
    midnight.tm_mon   = month - 1;
    midnight.tm_mday  = day;
    midnight.tm_isdst = 0;   /* UTC: DST is meaningless here */

    midnight_utc = SCHEDULER_TIMEGM(&midnight);
    if (midnight_utc == (time_t)-1) {
        return 0;
    }

    /* Local civil time -> UTC is local - offset (an east-of-Greenwich zone
       has a positive offset). Both terms are plain integer seconds, so a
       value past 24:00 (Isha, Islamic midnight) needs no special case. */
    return (int64_t)midnight_utc
         + (int64_t)local_sec
         - (int64_t)tz_offset_minutes * 60;
}

int scheduler_evaluate(int64_t now_utc_epoch,
                       const scheduler_day_t *today,
                       scheduler_state_t *state,
                       uint8_t fired_ids_out[5])
{
    int count = 0;

    if (!today || !state || !fired_ids_out) {
        return 0;
    }

    for (int p = 0; p < SCHEDULER_PRAYER_COUNT; p++) {
        int64_t epoch;

        if (!prayer_scheduled(today, p)) {
            continue;
        }

        epoch = scheduler_prayer_epoch(today->date_yyyymmdd,
                                       today->tz_offset_minutes,
                                       today->sec[p]);
        if (epoch == 0 || epoch > now_utc_epoch) {
            continue;   /* not scheduled / still in the future */
        }
        if (state->last_fired_epoch[p] >= epoch) {
            continue;   /* already fired for this instant */
        }

        /* Record before reporting: this is the guard that makes a repeated
           local hour (DST fall-back) or a backwards NTP step harmless. */
        state->last_fired_epoch[p] = epoch;
        fired_ids_out[count] = (uint8_t)p;
        count++;
    }

    return count;
}

void scheduler_premark_past(int64_t now_utc_epoch,
                            const scheduler_day_t *today,
                            scheduler_state_t *state)
{
    if (!today || !state) {
        return;
    }

    for (int p = 0; p < SCHEDULER_PRAYER_COUNT; p++) {
        int64_t epoch;

        if (!prayer_scheduled(today, p)) {
            continue;
        }

        epoch = scheduler_prayer_epoch(today->date_yyyymmdd,
                                       today->tz_offset_minutes,
                                       today->sec[p]);
        if (epoch == 0 || epoch > now_utc_epoch) {
            continue;
        }

        state->last_fired_epoch[p] = epoch;
    }
}
