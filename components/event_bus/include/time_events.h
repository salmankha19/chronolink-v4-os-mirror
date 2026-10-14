#ifndef EVENT_BUS_TIME_EVENTS_H
#define EVENT_BUS_TIME_EVENTS_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Event payloads for the time events published by svc_time. All fit inside
   event_t.data (see event_bus.h). Consumers cast evt.data to
   time_event_payload_t. */

/* Where the time came from. Stable numeric values. */
#define TIME_EVENT_SOURCE_RTC 0u  /* read back from the hardware RTC */
#define TIME_EVENT_SOURCE_NTP 1u  /* fresh NTP sync */

typedef struct {
    uint8_t  utc_seconds;   /* 0..59 */
    uint8_t  utc_minutes;   /* 0..59 */
    uint8_t  utc_hours;     /* 0..23 */
    uint8_t  utc_day;       /* 1..31 */
    uint8_t  utc_month;     /* 1..12 */
    uint16_t utc_year;      /* e.g. 2026 */
    uint8_t  state;         /* svc_time_state_t value (see svc_time.h) */
    uint8_t  source;        /* TIME_EVENT_SOURCE_* */
    uint8_t  _pad[3];
} time_event_payload_t;

/* UTC only: local (DST-adjusted) time is derived at read time from the
   POSIX TZ rule, so it is never carried as state. See docs/STATUS.md
   ("svc_time") and hal_rtc.h for the contract. */

_Static_assert(sizeof(time_event_payload_t) <= 32,
               "time_event_payload_t exceeds event_t.data size");

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_TIME_EVENTS_H */
