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
                                             midnight; UINT32_MAX = not
                                             scheduled (0 is a valid
                                             midnight instant, #136) */
    uint32_t date_yyyymmdd;               /* local calendar date of sec[] */
    int16_t  tz_offset_minutes;           /* local - UTC at noon of
                                             date_yyyymmdd */
    bool     valid;                       /* false = today's times unusable */
} scheduler_day_t;

/* Everything the scheduler remembers between ticks. */
typedef struct {
    int64_t last_fired_epoch[SCHEDULER_PRAYER_COUNT]; /* UTC epoch last fired
                                                         per prayer; 0 = never */
    /* Yesterday's Isha when it falls after local midnight and has not yet
       fired. Set by svc_scheduler.c at a true date rollover (never on a
       same-date re-fetch: that would double-fire once the state re-arms).
       Consumed by scheduler_evaluate() or scheduler_premark_past(). 0 = none. */
    int64_t pending_isha_epoch;
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
 * A prayer is skipped when it is not scheduled (sec == UINT32_MAX), when
 * today->valid is false, when its epoch is still in the future, when
 * it is more than SCHEDULER_MAX_LATENESS_S seconds in the past (treated as
 * missed: marked fired without being reported, so a long stall cannot
 * produce a burst of catch-up adhans), or when
 * state->last_fired_epoch[p] is already >= its epoch.
 *
 * If state->pending_isha_epoch is non-zero (yesterday's Isha fell after
 * local midnight and has not yet fired), that instant is also consumed:
 * reported with id 4 if within SCHEDULER_MAX_LATENESS_S, otherwise marked
 * fired without being published ("missed"). Today's Isha (slot 4) and
 * yesterday's pending Isha cannot both be inside the 120 s window
 * (they are 19+ hours apart), so fired_ids_out[5] remains sufficient.
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
 * Rebuild the scheduling state after the day or the times changed (boot,
 * EVENT_TIME_DATE_CHANGED, EVENT_PRAYER_TIMES_UPDATED).
 *
 *   out  = clean state, pending_isha_epoch = the value passed in,
 *          scheduler_premark_past(now, new_day) applied, then
 *          each prayer's last_fired_epoch raised to prev's marker when that
 *          marker is for an instant that has already happened (<= now).
 *
 * Why the carry-over: premark leaves SCHEDULER_PREMARK_GRACE_S un-marked so a
 * prayer due this very second is not swallowed. A prayer that fired a moment
 * ago is inside that window too; without the carry-over, wiping the state
 * would let it fire a second time. Markers for instants in the FUTURE (a
 * backwards NTP step after an early fire) are deliberately dropped, so the
 * prayer can fire again at its true time. prev may be NULL; it may alias out.
 */
void scheduler_rearm(int64_t now_utc_epoch,
                     const scheduler_day_t* new_day,
                     const scheduler_state_t* prev,
                     int64_t pending_isha_epoch,
                     scheduler_state_t* out);

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
