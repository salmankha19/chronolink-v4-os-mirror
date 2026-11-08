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
   real instant. 0 is local midnight and a valid instant (#136); UINT32_MAX
   is the engine's "cannot be computed / disabled" sentinel. The
   today->valid gate above is what protects against a zeroed struct -- a
   memset scheduler_day_t has sec[]==0 for all entries and would otherwise
   look like five midnights. */
static bool prayer_scheduled(const scheduler_day_t *today, int p)
{
    if (!today || !today->valid) {
        return false;
    }
    if (p < 0 || p >= SCHEDULER_PRAYER_COUNT) {
        return false;
    }
    return today->sec[p] != UINT32_MAX;
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

        /* Skip prayers more than SCHEDULER_MAX_LATENESS_S in the past --
           treat as missed rather than firing a burst. */
        if (now_utc_epoch - epoch > SCHEDULER_MAX_LATENESS_S) {
            state->last_fired_epoch[p] = epoch;   /* mark fired to avoid retry */
            continue;
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

    /* Consume yesterday's Isha if it was deferred past local midnight.
       Guard both branches with last_fired_epoch[4] < pending: if today's
       Isha (a later epoch) already fired this call, do not regress the
       marker or it will double-fire on the next tick. */
    if (state->pending_isha_epoch > 0 && count < SCHEDULER_PRAYER_COUNT) {
        int64_t pending = state->pending_isha_epoch;
        if (now_utc_epoch >= pending) {
            if (state->last_fired_epoch[4] < pending) {
                if (now_utc_epoch - pending <= SCHEDULER_MAX_LATENESS_S) {
                    state->last_fired_epoch[4] = pending;
                    fired_ids_out[count++] = 4;
                } else {
                    state->last_fired_epoch[4] = pending;   /* missed */
                }
            }
            /* else: today's Isha already fired at a later epoch -- leave it */
            state->pending_isha_epoch = 0;
        }
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
        if (epoch == 0 || epoch > now_utc_epoch - SCHEDULER_PREMARK_GRACE_S) {
            continue;   /* future, or inside the grace window: let evaluate() fire it */
        }

        state->last_fired_epoch[p] = epoch;
    }

    /* Same grace window as today's prayers: only premark the pending Isha
       once it is genuinely past, so a same-second reset does not swallow
       it. Guarded by last_fired_epoch[4] < pending for the same reason as
       in scheduler_evaluate(): never regress a later-epoch marker. */
    if (state->pending_isha_epoch > 0 &&
        state->pending_isha_epoch <= now_utc_epoch - SCHEDULER_PREMARK_GRACE_S) {
        if (state->last_fired_epoch[4] < state->pending_isha_epoch) {
            state->last_fired_epoch[4] = state->pending_isha_epoch;
        }
        state->pending_isha_epoch = 0;
    }
}

void scheduler_rearm(int64_t now_utc_epoch,
                     const scheduler_day_t *new_day,
                     const scheduler_state_t *prev,
                     int64_t pending_isha_epoch,
                     scheduler_state_t *out)
{
    scheduler_state_t carried;

    if (!out) {
        return;
    }

    /* prev may alias out: copy what we need before clearing. */
    memset(&carried, 0, sizeof(carried));
    if (prev) {
        carried = *prev;
    }

    memset(out, 0, sizeof(*out));
    out->pending_isha_epoch = pending_isha_epoch;
    scheduler_premark_past(now_utc_epoch, new_day, out);

    for (int p = 0; p < SCHEDULER_PRAYER_COUNT; p++) {
        int64_t m = carried.last_fired_epoch[p];
        if (m > out->last_fired_epoch[p] && m <= now_utc_epoch) {
            out->last_fired_epoch[p] = m;
        }
    }
}
