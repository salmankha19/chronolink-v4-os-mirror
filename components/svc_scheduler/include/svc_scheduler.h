/*
 * svc_scheduler.h
 *
 * ChronoLink V4 OS -- prayer time scheduler (service, layer 5).
 *
 * v1 is a 1 Hz poller: every second it compares the current UTC epoch with
 * today's adhan epochs and publishes EVENT_PRAYER_TIME_REACHED exactly once
 * per prayer per day. Matching is epoch-based by construction, so a repeated
 * local hour (DST fall-back) or an NTP step cannot double-fire or skip a
 * prayer. The decision logic lives in scheduler_logic.h (pure C, host-tested
 * in prayer_host_tests/test_scheduler.cpp).
 *
 * Out of scope in v1: DS3231 INT acceleration (ROADMAP Phase 6), jamat
 * firing (the payload already reserves a `kind` field), audio playback
 * (Phase 3), per-prayer DST offset (ROADMAP Phase 7).
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Prime the state from the current time and prayer times, marking everything
 * that has already passed today as fired, so a boot at 13:00 does not replay
 * the 10:00 and 12:00 adhans. Publishes nothing.
 *
 * Safe to call while svc_time or svc_prayer_engine_service still has no valid
 * time (boot order, missing location, untrusted RTC): the state simply stays
 * empty and the next event or tick fills it.
 *
 * Idempotent. Returns ESP_OK -- nothing here is fatal; a failure is logged at
 * DEBUG and self-heals on the next EVENT_PRAYER_TIMES_UPDATED / date change.
 */
esp_err_t svc_scheduler_init(void);

/*
 * Create the 1 Hz scheduler task (core 0) and subscribe to
 * EVENT_TIME_DATE_CHANGED and EVENT_PRAYER_TIMES_UPDATED. Non-blocking and
 * idempotent; calls svc_scheduler_init() first if it has not run yet.
 *
 * Returns ESP_ERR_NO_MEM if the mailbox queue or the task cannot be created.
 * A failed subscription only degrades to "the 1 Hz poll discovers the change
 * within a second", so it is logged as a warning, not returned.
 */
esp_err_t svc_scheduler_start(void);

#ifdef __cplusplus
}
#endif
