/*
 * svc_time.h
 *
 * ChronoLink V4 OS -- time service (public API)
 *
 * Owns the clock:
 *   - the RTC (through the HIL rtc_api) holds UTC; it is never written
 *     with a local or DST-adjusted value -- see docs/STATUS.md ("svc_time")
 *     and hal_rtc.h for the contract.
 *   - NTP (esp_netif_sntp) is the only sync source and provides UTC.
 *     Three server slots live in NVS namespace "time"; see svc_time_start().
 *   - local time is derived at read time through the POSIX TZ mechanism
 *     (setenv("TZ") + tzset() + localtime_r()), so DST is the libc's job.
 *
 * Events: publishes EVENT_TIME_UPDATE (payload time_event_payload_t, see
 * time_events.h) after every successful NTP sync; subscribes to
 * EVENT_NETWORK_CONNECTED to trigger one.
 *
 * NOT in scope: prayer times, display formatting, the config UI (svc_http).
 * The web UI resolves an IANA zone name to a POSIX TZ string and calls
 * svc_time_set_timezone_posix(); the device never parses IANA tzdata.
 *
 * Non-fatal by design: no WiFi, no NTP, or no RTC still leaves the clock
 * running (see svc_time_is_trustworthy()).
 *
 * Threading: svc_time_start() is called once from app_main; the getters and
 * setters may be called from any task (they take the service lock).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rtc_api.h"   /* rtc_time_t -- the HIL adapter, never hal_rtc.h */

#ifdef __cplusplus
extern "C" {
#endif

/* Capacity of the stored POSIX TZ string, including the terminating NUL.
   Longest realistic value is ~27 chars ("NZST-12NZDT,M9.5.0,M4.1.0/3"). */
#define SVC_TIME_TZ_MAX        64

/* Number of NTP server slots (NVS keys ntp_server_0 .. ntp_server_2). */
#define SVC_TIME_NTP_SLOTS     3

/* How much the current time can be trusted. Stable numeric values; the
   same values travel in time_event_payload_t.state. */
typedef enum {
    SVC_TIME_STATE_UNSYNCED   = 0,  /* nothing usable yet */
    SVC_TIME_STATE_RTC_ONLY   = 1,  /* read from the RTC; no NTP yet this boot */
    SVC_TIME_STATE_NTP_SYNCED = 2,  /* NTP succeeded this boot */
} svc_time_state_t;

/* One consistent view of the clock. `utc` is what was written to the RTC;
   `local` is `utc` with the POSIX TZ rule applied. Both are invalid while
   `valid` is false. */
typedef struct {
    rtc_time_t       utc;
    rtc_time_t       local;
    svc_time_state_t state;
    bool             valid;
} svc_time_snapshot_t;

/*
 * Start the service: load the NVS configuration (namespace "time": keys
 * "tz" and "ntp_server_0..2"), apply the timezone, read the RTC, create the
 * worker task and subscribe to EVENT_NETWORK_CONNECTED.
 *
 * Non-blocking: the first NTP sync happens when the network reports
 * connected. Idempotent.
 *
 * Returns ESP_FAIL only if the mailbox, the locks or the task could not be
 * created. A missing NVS namespace or a missing RTC is logged and
 * tolerated: the service then runs on the built-in defaults ("GMT0" and the
 * three default NTP servers) and stays UNSYNCED until NTP succeeds.
 */
esp_err_t svc_time_start(void);

/*
 * Replace the POSIX TZ string, e.g. "GMT0" or
 * "GMT0BST,M3.5.0/1,M10.5.0" (Europe/London). Persisted to NVS key "tz",
 * but does NOT force a resync -- the new zone is applied immediately and to
 * all later reads; a resync happens on the next EVENT_NETWORK_CONNECTED or
 * backoff retry.
 *
 * Returns ESP_ERR_INVALID_ARG if tz is NULL, empty, or >= SVC_TIME_TZ_MAX;
 * ESP_ERR_INVALID_STATE if svc_time_start() has not run; otherwise the NVS
 * error if the write failed (the zone is still applied in RAM).
 */
esp_err_t svc_time_set_timezone_posix(const char *tz);

/* Current POSIX TZ string ("" before svc_time_start() completes). Owned by
   the service; do not free. Only svc_time_set_timezone_posix() changes it,
   under the service lock -- copy it if you need it across a concurrent set. */
const char *svc_time_get_timezone_posix(void);

/* Copy the snapshot. Returns ESP_ERR_INVALID_ARG if out is NULL and
   ESP_ERR_INVALID_STATE if the service is not started. Always fills `out`
   when it returns ESP_OK; check out->valid before using the times. */
esp_err_t svc_time_get_snapshot(svc_time_snapshot_t *out);

/* Same guards as svc_time_get_snapshot(), but return ESP_ERR_INVALID_STATE
   (and leave *out untouched) while there is no trustworthy time. */
esp_err_t svc_time_get_local(rtc_time_t *out);
esp_err_t svc_time_get_utc(rtc_time_t *out);

/*
 * Run one NTP sync now, in the caller's task. BLOCKS for up to 10 s plus any
 * sync already in flight. Do not call it from the kernel event dispatcher
 * task (core_os' event_disp) or from an esp_timer callback. Intended for a
 * future web UI "sync now" button.
 *
 * Returns ESP_ERR_INVALID_STATE before svc_time_start(), while another sync
 * is running, or if the network stack is not up yet (svc_network has not
 * finished esp_netif_init()).
 */
esp_err_t svc_time_sync_ntp_once(void);

/* True once NTP produced a time this boot (valid && NTP_SYNCED). Does not
   block. False before svc_time_start(). */
bool svc_time_is_trustworthy(void);

/*
 * Override one NTP server slot (0..SVC_TIME_NTP_SLOTS-1). Persisted to NVS
 * key "ntp_server_<slot>", but does NOT force a resync (the next
 * EVENT_NETWORK_CONNECTED or backoff retry picks it up).
 *
 * Absent slot -> built-in default for that slot; a slot stored as an empty
 * string is skipped. This call refuses an empty host, so "disable slot N"
 * is done by writing the empty string directly (web UI / factory tooling),
 * not through this API.
 *
 * Returns ESP_ERR_INVALID_ARG for slot >= SVC_TIME_NTP_SLOTS or a
 * NULL/empty/too-long host; ESP_ERR_INVALID_STATE if the service is not
 * started; otherwise the NVS error if the write failed (the host is still
 * used in RAM).
 */
esp_err_t svc_time_set_ntp_server(uint8_t slot, const char *host);

#ifdef __cplusplus
}
#endif
