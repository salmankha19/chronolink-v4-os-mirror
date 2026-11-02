/*
 * scheduler_logic.h
 *
 * ChronoLink V4 OS -- prayer scheduler decision logic (pure C).
 *
 * No ESP-IDF, no FreeRTOS, no heap: this header and scheduler_logic.c must
 * compile with a plain host compiler so the DST / NTP-jump behaviour can be
 * unit-tested off-target (prayer_host_tests/test_scheduler.cpp).
 *
 * Design invariant: prayer instants are matched by EPOCH comparison only.
 * There is no (hour, minute) equality anywhere, so a repeated local hour
 * (DST fall-back) or an NTP step cannot double-fire or skip a prayer. All
 * times are computed as UTC epochs from (local date, local seconds, noon
 * UTC offset) -- the same convention svc_prayer_engine_service publishes.
 *
 * Prayer index order matches svc_config_prayer_t:
 *   0 = Fajr, 1 = Dhuhr, 2 = Asr, 3 = Maghrib, 4 = Isha
 */

/*
 * LIMITATION: Isha after local midnight is not supported at date rollover.
 * If a calculation produces Isha past local 24:00, the scheduler drops it
 * at the next day's rollover. This affects far-north locations with
 * Umm al-Qura or Moonsighting rules in summer. A full fix requires the
 * adapter (svc_scheduler.c) to publish tomorrow's Isha into today's
 * scheduler_day_t -- deferred to a dedicated session because it changes
 * the contract between svc_prayer_engine_service and svc_scheduler.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCHEDULER_PRAYER_COUNT 5

/* scheduler_premark_past() leaves the most recent seconds un-marked. The 1 Hz
   poll can be up to a second behind a prayer instant; a premark that landed in
   that gap (EVENT_PRAYER_TIMES_UPDATED processed in the same loop iteration)
   would silently swallow the adhan. Anything older than this is "past". */
#define SCHEDULER_PREMARK_GRACE_S 2

/* A single late tick fires every overdue prayer (catch-up semantics). This
   threshold caps how late "overdue" can be: a prayer more than this many
   seconds in the past is treated as missed, not fired, to prevent a burst
   of five back-to-back adhans after a long stall (e.g., >2 min of downtime).
   A forward clock step larger than this (an NTP correction) skips the
   prayers inside the skipped interval for the same reason. */
#define SCHEDULER_MAX_LATENESS_S 120

/* Today's adhan times, in the shape svc_scheduler.c adapts from
   svc_prayer_engine_times_t (adhan tier only). */
typedef struct {
    uint32_t sec[SCHEDULER_PRAYER_COUNT]; /* adhan seconds since local
                                             midnight; 0 or UINT32_MAX =
                                             not scheduled */
    uint32_t date_yyyymmdd;               /* local calendar date of sec[] */
    int16_t  tz_offset_minutes;           /* local - UTC at noon of
                                             date_yyyymmdd */
    bool     valid;                       /* false = today's times unusable */
} scheduler_day_t;

/* Everything the scheduler remembers between ticks. */
typedef struct {
    int64_t last_fired_epoch[SCHEDULER_PRAYER_COUNT]; /* UTC epoch last fired
                                                         per prayer; 0 = never */
} scheduler_state_t;

/*
 * Compare now_utc_epoch with today's prayer epochs and fire each prayer at
 * most once.
 *
 * Returns the count of prayers to fire and fills fired_ids_out[0..count-1]
 * with their indices; the caller publishes the events. Mutates
 * state->last_fired_epoch for every prayer it reports, so calling it again
 * with the same `now` returns 0 (idempotent).
 *
 * A prayer is skipped when it is not scheduled (sec == 0 or UINT32_MAX),
 * when today->valid is false, when its epoch is still in the future, when
 * it is more than SCHEDULER_MAX_LATENESS_S seconds in the past (treated as
 * missed: marked fired without being reported, so a long stall cannot
 * produce a burst of catch-up adhans), or when
 * state->last_fired_epoch[p] is already >= its epoch.
 */
int scheduler_evaluate(int64_t now_utc_epoch,
                       const scheduler_day_t* today,
                       scheduler_state_t* state,
                       uint8_t fired_ids_out[5]);

/*
 * Pre-mark all prayers with epoch <= now_utc_epoch - SCHEDULER_PREMARK_GRACE_S as already fired, without
 * reporting anything (no event is published). Call it at init and after
 * re-fetching times on EVENT_TIME_DATE_CHANGED / EVENT_PRAYER_TIMES_UPDATED,
 * so a boot or a config change in the middle of the day does not replay the
 * prayers that have already passed.
 *
 * Never clears an entry: the caller resets the state (memset) when the times
 * change, then calls this to re-arm the past.
 */
void scheduler_premark_past(int64_t now_utc_epoch,
                            const scheduler_day_t* today,
                            scheduler_state_t* state);

/*
 * Convert (date, local_sec, tz_offset) to a UTC epoch.
 *
 * local_sec may exceed 86400 (Isha / Islamic midnight can fall after local
 * midnight); the arithmetic handles that without wrapping.
 * Returns 0 for an invalid date, for local_sec == UINT32_MAX, or if the
 * conversion fails -- 0 is outside the representable range for every date
 * this firmware accepts (year >= 1970).
 *
 * Exposed for testability.
 */
int64_t scheduler_prayer_epoch(uint32_t date_yyyymmdd,
                               int16_t tz_offset_minutes,
                               uint32_t local_sec);

#ifdef __cplusplus
}
#endif
