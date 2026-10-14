/*
 * svc_time.c
 *
 * ChronoLink V4 OS -- time service.
 *
 * Responsibilities:
 *   - own the UTC clock: read the RTC through the HIL (rtc_api), set it from NTP
 *   - turn EVENT_NETWORK_CONNECTED into an NTP sync, with backoff retry
 *   - derive local (DST-adjusted) time from UTC through the POSIX TZ rule
 *   - publish EVENT_TIME_UPDATE once a new UTC time has been accepted
 *
 * NOT here: prayer times, display formatting, the config UI (svc_http).
 *
 * The RTC holds UTC. A local or DST-adjusted value is never written to it;
 * see docs/STATUS.md ("svc_time") and hal_rtc.h for the contract.
 *
 * Threading:
 *   worker task  svc_time_worker   -- mailbox drain, syncs, status print
 *   esp_timer    backoff_timer_cb  -- retry after a failed sync; it calls
 *                                    run_ntp_sync() directly (design note in
 *                                    docs/STATUS.md), which blocks that task
 *                                    for up to SVC_TIME_SYNC_WAIT_MS
 *   callers      svc_time_sync_ntp_once() runs a sync in the caller's task
 *
 * Locks:
 *   s_state_lock -- guards s_snapshot, s_tz and s_servers, and the RTC
 *                   access that produces a snapshot
 *   s_sync_lock  -- one NTP sync at a time; esp_netif_sntp_deinit() frees
 *                   the semaphore a concurrent esp_netif_sntp_sync_wait()
 *                   would be blocked on
 */

#include "svc_time.h"

#include <stdio.h>
#include <stdlib.h>   /* setenv */
#include <string.h>
#include <time.h>     /* time, timegm, gmtime_r, localtime_r, tzset */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs.h"

#include "event_bus.h"
#include "time_events.h"

static const char *TAG = "svc_time";

/* ------------------------------------------------------------------ */
/* Configuration                                                      */
/* ------------------------------------------------------------------ */

#define NVS_NAMESPACE_TIME     "time"
#define NVS_KEY_TZ             "tz"
#define NVS_KEY_NTP_PREFIX     "ntp_server_"   /* + "0" .. "2" */

#define SVC_TIME_NTP_HOST_MAX  64              /* incl. NUL */
#define SVC_TIME_DEFAULT_TZ    "GMT0"

static const char *const k_ntp_defaults[SVC_TIME_NTP_SLOTS] = {
    "time.google.com",
    "time.cloudflare.com",
    "0.uk.pool.ntp.org",
};

/* Failed-sync retry sequence: 1 min -> 5 min -> 15 min -> 1 h cap. */
static const uint32_t k_backoff_ms[] = {
    1u * 60u * 1000u, 5u * 60u * 1000u, 15u * 60u * 1000u, 60u * 60u * 1000u,
};
#define SVC_TIME_BACKOFF_STEPS (sizeof(k_backoff_ms) / sizeof(k_backoff_ms[0]))

/* A sync younger than this is not repeated: EVENT_NETWORK_CONNECTED can
   arrive twice for one link, and the backoff timer can fire just after a
   manual sync. IDF's own 1 h cycle covers the long term. */
#define SVC_TIME_RESYNC_MIN_MS      60000u

/* Must exceed the worst case of CONFIG_LWIP_SNTP_STARTUP_DELAY (5 s). */
#define SVC_TIME_SYNC_WAIT_MS       10000u

#define SVC_TIME_MAILBOX_LEN        4
#define SVC_TIME_TASK_STACK         4096
#define SVC_TIME_TASK_PRIO          (tskIDLE_PRIORITY + 3)
#define SVC_TIME_TASK_CORE          0
#define SVC_TIME_STATUS_PERIOD_MS   5000  /* TEMPORARY status print period */

#define SVC_TIME_LOCK_TICKS         pdMS_TO_TICKS(1000)
#define SVC_TIME_SYNC_LOCK_TICKS    pdMS_TO_TICKS(100)

/* ------------------------------------------------------------------ */
/* Internal state                                                     */
/* ------------------------------------------------------------------ */

static bool                s_started          = false;
static bool                s_nvs_open         = false;
static bool                s_sntp_initialized = false;
static nvs_handle_t        s_nvs;

static QueueHandle_t       s_mailbox          = NULL;
static TaskHandle_t        s_worker           = NULL;
static SemaphoreHandle_t   s_state_lock       = NULL;
static SemaphoreHandle_t   s_sync_lock        = NULL;
static esp_timer_handle_t  s_backoff_timer    = NULL;

static svc_time_snapshot_t s_snapshot;                    /* s_state_lock */
static char                s_tz[SVC_TIME_TZ_MAX];         /* s_state_lock */
static char                s_servers[SVC_TIME_NTP_SLOTS][SVC_TIME_NTP_HOST_MAX];
/* The buffers actually handed to LwIP. lwip's sntp_setservername() stores
   the POINTER, it does not copy the string, so they must stay valid and
   unchanged for as long as an SNTP session runs: static, rewritten only
   while s_sync_lock is held and only after the previous session was
   deinitialised. */
static char                s_lwip_servers[SVC_TIME_NTP_SLOTS][SVC_TIME_NTP_HOST_MAX];

static size_t              s_backoff_idx      = 0;
static int64_t             s_last_sync_us     = 0;

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

/* Returns false when the lock could not be taken; the caller must then not
   touch the guarded state and must not call state_unlock(). */
static bool state_lock(void)
{
    return s_state_lock && xSemaphoreTake(s_state_lock, SVC_TIME_LOCK_TICKS) == pdTRUE;
}

static void state_unlock(void)
{
    xSemaphoreGive(s_state_lock);
}

static const char *state_to_str(svc_time_state_t state)
{
    switch (state) {
    case SVC_TIME_STATE_RTC_ONLY:   return "RTC_ONLY";
    case SVC_TIME_STATE_NTP_SYNCED: return "NTP_SYNCED";
    case SVC_TIME_STATE_UNSYNCED:
    default:                        return "UNSYNCED";
    }
}

static rtc_time_t tm_to_rtc(const struct tm *tm)
{
    rtc_time_t t;

    t.seconds = (uint8_t)tm->tm_sec;
    t.minutes = (uint8_t)tm->tm_min;
    t.hours   = (uint8_t)tm->tm_hour;
    t.day     = (uint8_t)tm->tm_mday;
    t.month   = (uint8_t)(tm->tm_mon + 1);
    t.year    = (uint16_t)(tm->tm_year + 1900);
    return t;
}

/* Broken-down UTC -> epoch seconds. timegm() is the inverse of gmtime_r():
   it reads the struct as UTC, which is what the RTC holds. */
static bool utc_to_epoch(const rtc_time_t *utc, time_t *epoch)
{
    struct tm tm_utc;
    time_t e;

    if (utc->month < 1 || utc->month > 12 || utc->day < 1 || utc->day > 31) {
        return false;
    }
    memset(&tm_utc, 0, sizeof(tm_utc));
    tm_utc.tm_sec   = utc->seconds;
    tm_utc.tm_min   = utc->minutes;
    tm_utc.tm_hour  = utc->hours;
    tm_utc.tm_mday  = utc->day;
    tm_utc.tm_mon   = (int)utc->month - 1;
    tm_utc.tm_year  = (int)utc->year - 1900;
    tm_utc.tm_isdst = 0;

    e = timegm(&tm_utc);
    if (e == (time_t)-1) {
        return false;
    }
    *epoch = e;
    return true;
}

/* UTC -> local through the POSIX TZ rule last handed to the libc. */
static bool utc_to_local(rtc_time_t *out, const rtc_time_t *utc)
{
    struct tm tm_local;
    time_t epoch;

    if (!utc_to_epoch(utc, &epoch)) {
        return false;
    }
    if (localtime_r(&epoch, &tm_local) == NULL) {
        return false;
    }
    *out = tm_to_rtc(&tm_local);
    return true;
}

static void apply_timezone(void)
{
    /* setenv() copies the string and tzset() re-reads TZ; both are global
       libc state, which is why the zone is applied before a local time is
       derived from it. */
    if (setenv("TZ", s_tz, 1) != 0) {
        ESP_LOGE(TAG, "setenv(TZ, '%s') failed -- local time will be wrong", s_tz);
        return;
    }
    tzset();
    ESP_LOGI(TAG, "TZ applied: '%s'", s_tz);
}

/* Store a new UTC value and derive local from it. Caller holds the lock. */
static void snapshot_store_locked(const rtc_time_t *utc, svc_time_state_t state)
{
    rtc_time_t local = *utc;   /* fall back to UTC if the zone rule fails */

    if (!utc_to_local(&local, utc)) {
        ESP_LOGW(TAG, "cannot derive local time (TZ='%s') -- reporting UTC", s_tz);
    }
    s_snapshot.utc   = *utc;
    s_snapshot.local = local;
    s_snapshot.state = state;
    s_snapshot.valid = true;
}

static void publish_time_update(const rtc_time_t *utc, svc_time_state_t state,
                                uint8_t source)
{
    time_event_payload_t p;
    event_t evt;

    memset(&p, 0, sizeof(p));
    p.utc_seconds = utc->seconds;
    p.utc_minutes = utc->minutes;
    p.utc_hours   = utc->hours;
    p.utc_day     = utc->day;
    p.utc_month   = utc->month;
    p.utc_year    = utc->year;
    p.state       = (uint8_t)state;
    p.source      = source;

    memset(&evt, 0, sizeof(evt));
    evt.type = EVENT_TIME_UPDATE;
    memcpy(evt.data, &p, sizeof(p));
    event_bus_publish(&evt);
}

static bool sync_is_fresh(void)
{
    int64_t last;

    if (!state_lock()) {
        return false;
    }
    last = s_last_sync_us;
    state_unlock();

    return last != 0 &&
           (esp_timer_get_time() - last) < ((int64_t)SVC_TIME_RESYNC_MIN_MS * 1000);
}

/* ------------------------------------------------------------------ */
/* NVS configuration                                                  */
/* ------------------------------------------------------------------ */

static void load_timezone_from_nvs(void)
{
    size_t len = sizeof(s_tz);
    esp_err_t err = ESP_ERR_NVS_NOT_FOUND;

    s_tz[0] = '\0';
    if (s_nvs_open) {
        err = nvs_get_str(s_nvs, NVS_KEY_TZ, s_tz, &len);
    }
    if (err == ESP_OK && s_tz[0] != '\0') {
        ESP_LOGI(TAG, "TZ from NVS: '%s'", s_tz);
        return;
    }
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "NVS '%s' is empty -- using '%s'",
                 NVS_KEY_TZ, SVC_TIME_DEFAULT_TZ);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS '%s' absent -- using '%s' (UTC)",
                 NVS_KEY_TZ, SVC_TIME_DEFAULT_TZ);
    } else {
        ESP_LOGW(TAG, "NVS read of '%s' failed: %s -- using '%s'",
                 NVS_KEY_TZ, esp_err_to_name(err), SVC_TIME_DEFAULT_TZ);
    }
    strncpy(s_tz, SVC_TIME_DEFAULT_TZ, sizeof(s_tz) - 1);
    s_tz[sizeof(s_tz) - 1] = '\0';
}

/* Per-slot semantics (docs/STATUS.md): absent -> built-in default, present
   but empty -> slot disabled, anything else -> user hostname. */
static void load_servers_from_nvs(void)
{
    for (size_t slot = 0; slot < SVC_TIME_NTP_SLOTS; slot++) {
        char key[sizeof(NVS_KEY_NTP_PREFIX) + 4];
        char host[SVC_TIME_NTP_HOST_MAX];
        size_t len = sizeof(host);
        esp_err_t err = ESP_ERR_NVS_NOT_FOUND;

        snprintf(key, sizeof(key), "%s%u", NVS_KEY_NTP_PREFIX, (unsigned)slot);
        host[0] = '\0';
        if (s_nvs_open) {
            err = nvs_get_str(s_nvs, key, host, &len);
        }

        if (err == ESP_OK) {
            strncpy(s_servers[slot], host, SVC_TIME_NTP_HOST_MAX - 1);
            s_servers[slot][SVC_TIME_NTP_HOST_MAX - 1] = '\0';
            if (s_servers[slot][0] == '\0') {
                ESP_LOGI(TAG, "NTP slot %u disabled (empty value)", (unsigned)slot);
            } else {
                ESP_LOGI(TAG, "NTP slot %u from NVS: '%s'",
                         (unsigned)slot, s_servers[slot]);
            }
            continue;
        }

        strncpy(s_servers[slot], k_ntp_defaults[slot], SVC_TIME_NTP_HOST_MAX - 1);
        s_servers[slot][SVC_TIME_NTP_HOST_MAX - 1] = '\0';
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "NTP slot %u absent -- default '%s'",
                     (unsigned)slot, s_servers[slot]);
        } else {
            ESP_LOGW(TAG, "NVS read of '%s' failed: %s -- default '%s'",
                     key, esp_err_to_name(err), s_servers[slot]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Backoff: 1 min -> 5 min -> 15 min -> 1 h cap                       */
/* ------------------------------------------------------------------ */

static void cancel_backoff(void)
{
    s_backoff_idx = 0;
    if (s_backoff_timer) {
        esp_timer_stop(s_backoff_timer);   /* no-op if not armed */
    }
}

static void arm_backoff(void)
{
    uint32_t delay_ms;
    esp_err_t err;

    if (!s_backoff_timer) {
        return;
    }
    delay_ms = k_backoff_ms[s_backoff_idx];
    esp_timer_stop(s_backoff_timer);       /* no-op if not armed */
    err = esp_timer_start_once(s_backoff_timer, (uint64_t)delay_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_timer_start_once failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGW(TAG, "NTP retry in %lu s (backoff step %u of %u)",
             (unsigned long)(delay_ms / 1000u),
             (unsigned)(s_backoff_idx + 1), (unsigned)SVC_TIME_BACKOFF_STEPS);
    if (s_backoff_idx + 1 < SVC_TIME_BACKOFF_STEPS) {
        s_backoff_idx++;
    }
}

/* ------------------------------------------------------------------ */
/* NTP sync                                                           */
/* ------------------------------------------------------------------ */

/* esp_netif_sntp_* hand their work to the LwIP tcpip thread, which
   svc_network creates (esp_netif_init + esp_netif_create_default_wifi_sta).
   Refuse to touch SNTP before that exists. esp_netif_get_nr_of_ifs() is a
   plain counter read -- safe at any time. */
static bool network_stack_ready(void)
{
    return esp_netif_get_nr_of_ifs() > 0;
}

/* Copy the usable hostnames out of s_servers and return how many there are
   (0 = every slot disabled). Caller holds the lock: the setter rewrites
   s_servers. */
static size_t collect_servers_locked(char bufs[SVC_TIME_NTP_SLOTS][SVC_TIME_NTP_HOST_MAX])
{
    size_t n = 0;

    for (size_t slot = 0; slot < SVC_TIME_NTP_SLOTS; slot++) {
        if (s_servers[slot][0] == '\0') {
            continue;   /* slot disabled: an empty NVS value means "skip" */
        }
        strncpy(bufs[n], s_servers[slot], SVC_TIME_NTP_HOST_MAX - 1);
        bufs[n][SVC_TIME_NTP_HOST_MAX - 1] = '\0';
        n++;
    }
    return n;
}

static esp_err_t do_ntp_sync(const char *why)
{
    esp_sntp_config_t cfg;
    esp_err_t err;
    size_t n;
    time_t epoch;
    struct tm tm_utc;
    rtc_time_t utc;
    rtc_time_t local_copy;
    char tz_copy[SVC_TIME_TZ_MAX];

    if (sync_is_fresh()) {
        ESP_LOGI(TAG, "sync skipped (%s): last sync is younger than %u s",
                 why, (unsigned)(SVC_TIME_RESYNC_MIN_MS / 1000u));
        return ESP_OK;
    }
    if (!network_stack_ready()) {
        ESP_LOGW(TAG, "sync skipped (%s): network stack not up yet", why);
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop any previous session first: LwIP holds pointers into
       s_lwip_servers, which is rewritten just below. */
    if (s_sntp_initialized) {
        esp_netif_sntp_deinit();
        s_sntp_initialized = false;
    }

    if (!state_lock()) {
        ESP_LOGE(TAG, "state lock unavailable -- sync aborted (%s)", why);
        return ESP_ERR_TIMEOUT;
    }
    n = collect_servers_locked(s_lwip_servers);
    state_unlock();

    if (n == 0) {
        ESP_LOGW(TAG, "sync skipped (%s): every NTP slot is disabled", why);
        return ESP_ERR_INVALID_STATE;
    }
    if (n > (size_t)CONFIG_LWIP_SNTP_MAX_SERVERS) {
        ESP_LOGW(TAG, "%u NTP slots configured but CONFIG_LWIP_SNTP_MAX_SERVERS=%u"
                      " -- extra slots unused (stale sdkconfig.single_mcu?)",
                 (unsigned)n, (unsigned)CONFIG_LWIP_SNTP_MAX_SERVERS);
        n = (size_t)CONFIG_LWIP_SNTP_MAX_SERVERS;
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.smooth_sync    = false;  /* step the clock: a display clock gains nothing from slew */
    cfg.wait_for_sync  = true;   /* creates the semaphore sync_wait() blocks on */
    cfg.start          = false;  /* started explicitly below */
    cfg.num_of_servers = n;
    for (size_t i = 0; i < n; i++) {
        cfg.servers[i] = s_lwip_servers[i];   /* static: LwIP keeps the pointer */
    }

    err = esp_netif_sntp_init(&cfg);
    if (err == ESP_OK) {
        s_sntp_initialized = true;
        err = esp_netif_sntp_start();
    }
    if (err == ESP_OK) {
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SVC_TIME_SYNC_WAIT_MS));
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NTP sync failed (%s): %s -- keeping the old time",
                 why, esp_err_to_name(err));
        arm_backoff();
        return err;   /* non-fatal: the clock keeps running off the RTC */
    }

    epoch = time(NULL);
    if (epoch <= 0 || gmtime_r(&epoch, &tm_utc) == NULL) {
        ESP_LOGW(TAG, "NTP reported success but the epoch is unusable (%lld)",
                 (long long)epoch);
        arm_backoff();
        return ESP_FAIL;
    }
    utc = tm_to_rtc(&tm_utc);

    if (!state_lock()) {
        ESP_LOGE(TAG, "state lock unavailable -- NTP result dropped");
        arm_backoff();
        return ESP_ERR_TIMEOUT;
    }

    /* The RTC write is best effort: the snapshot already holds the value, so
       a board without a DS3231M degrades to "correct until the next reset".
       The oscillator-stop flag is cleared only once a trustworthy time has
       arrived, and never before. */
    if (hil_rtc_set_time(&utc)) {
        if (!hil_rtc_clear_lost_power_flag()) {
            ESP_LOGW(TAG, "hil_rtc_clear_lost_power_flag() failed -- OSF may keep tripping");
        }
    } else {
        ESP_LOGW(TAG, "hil_rtc_set_time() failed (no RTC?) -- UTC kept in RAM only");
    }
    snapshot_store_locked(&utc, SVC_TIME_STATE_NTP_SYNCED);
    local_copy = s_snapshot.local;
    memcpy(tz_copy, s_tz, sizeof(tz_copy));
    s_last_sync_us = esp_timer_get_time();
    state_unlock();

    cancel_backoff();   /* the next failure starts again at 1 min */
    publish_time_update(&utc, SVC_TIME_STATE_NTP_SYNCED, TIME_EVENT_SOURCE_NTP);

    ESP_LOGI(TAG, "NTP sync ok (%s): UTC %04u-%02u-%02u %02u:%02u:%02u"
                  " -> local %02u:%02u:%02u (TZ='%s')",
             why, (unsigned)utc.year, (unsigned)utc.month, (unsigned)utc.day,
             (unsigned)utc.hours, (unsigned)utc.minutes, (unsigned)utc.seconds,
             (unsigned)local_copy.hours, (unsigned)local_copy.minutes,
             (unsigned)local_copy.seconds, tz_copy);
    return ESP_OK;
}

/* One sync at a time: the worker, the backoff timer and
   svc_time_sync_ntp_once() can all reach this. */
static esp_err_t run_ntp_sync(const char *why)
{
    esp_err_t err;

    if (!s_sync_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_sync_lock, SVC_TIME_SYNC_LOCK_TICKS) != pdTRUE) {
        ESP_LOGW(TAG, "a sync is already running -- ignoring the %s request", why);
        return ESP_ERR_INVALID_STATE;
    }

    err = do_ntp_sync(why);
    xSemaphoreGive(s_sync_lock);
    return err;
}

static void backoff_timer_cb(void *arg)
{
    esp_err_t err;

    (void)arg;
    /* Runs in the esp_timer task, not on the worker (design note in
       docs/STATUS.md). esp_netif_sntp_sync_wait() blocks that task for up to
       SVC_TIME_SYNC_WAIT_MS, which also delays every other esp_timer. The
       wait is bounded and only happens while NTP is failing; if that ever
       becomes a problem, signal the worker from here instead. */
    ESP_LOGI(TAG, "backoff elapsed -- retrying NTP");
    err = run_ntp_sync("backoff");
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "backoff retry result: %s", esp_err_to_name(err));
    }
}

/* ------------------------------------------------------------------ */
/* Worker                                                             */
/* ------------------------------------------------------------------ */

/* TEMPORARY: remove when the display path lands. */
static void log_local_status(void)
{
    svc_time_snapshot_t snap;

    if (svc_time_get_snapshot(&snap) != ESP_OK) {
        ESP_LOGW(TAG, "status: snapshot unavailable");
        return;
    }
    if (!snap.valid) {
        ESP_LOGI(TAG, "status: no valid time yet (state=%s)", state_to_str(snap.state));
        return;
    }
    ESP_LOGI(TAG, "status: local %02u:%02u:%02u (UTC %02u:%02u:%02u) state=%s",
             (unsigned)snap.local.hours, (unsigned)snap.local.minutes,
             (unsigned)snap.local.seconds, (unsigned)snap.utc.hours,
             (unsigned)snap.utc.minutes, (unsigned)snap.utc.seconds,
             state_to_str(snap.state));
}

static void svc_time_worker(void *arg)
{
    event_t evt;

    (void)arg;
    ESP_LOGI(TAG, "worker started");

    for (;;) {
        if (xQueueReceive(s_mailbox, &evt,
                          pdMS_TO_TICKS(SVC_TIME_STATUS_PERIOD_MS)) == pdTRUE) {
            switch (evt.type) {
            case EVENT_NETWORK_CONNECTED: {
                /* A fresh link may be exactly what the last attempt needed,
                   so cancel the pending backoff and retry now. */
                esp_err_t err;
                ESP_LOGI(TAG, "network connected -- starting NTP sync");
                cancel_backoff();
                err = run_ntp_sync("network");
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "NTP sync after connect failed: %s",
                             esp_err_to_name(err));
                }
                break;
            }
            default:
                ESP_LOGD(TAG, "ignoring event type=%d", (int)evt.type);
                break;
            }
            continue;
        }

        log_local_status();   /* TEMPORARY: remove with the status print */
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

/* Cleanup-only-error path: reached only before s_started is set, so nothing
   else can be using these objects. */
static void cleanup_time_objects(void)
{
    if (s_backoff_timer) {
        esp_timer_delete(s_backoff_timer);
        s_backoff_timer = NULL;
    }
    if (s_mailbox) {
        vQueueDelete(s_mailbox);
        s_mailbox = NULL;
    }
    if (s_state_lock) {
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
    }
    if (s_sync_lock) {
        vSemaphoreDelete(s_sync_lock);
        s_sync_lock = NULL;
    }
}

/* Read whatever the RTC can give us. Called from svc_time_start() before any
   other task exists, so the snapshot is written directly (no lock yet).
   A missing RTC or a set oscillator-stop flag leaves the service UNSYNCED:
   whatever the register says then is meaningless (see hal_rtc.h). */
static void load_initial_time_from_rtc(void)
{
    rtc_time_t rtc_now;
    bool lost_power = true;

    if (!hil_rtc_init()) {
        ESP_LOGW(TAG, "hil_rtc_init() failed (no DS3231M?) -- waiting for NTP");
        return;
    }
    if (!hil_rtc_has_lost_power(&lost_power)) {
        ESP_LOGW(TAG, "hil_rtc_has_lost_power() failed -- treating the RTC as untrusted");
        lost_power = true;
    }
    if (lost_power) {
        ESP_LOGW(TAG, "RTC oscillator stopped (OSF) -- waiting for NTP");
        return;
    }
    if (!hil_rtc_get_time(&rtc_now)) {
        ESP_LOGW(TAG, "hil_rtc_get_time() failed -- waiting for NTP");
        return;
    }

    s_snapshot.utc = rtc_now;
    s_snapshot.local = rtc_now;
    if (!utc_to_local(&s_snapshot.local, &rtc_now)) {
        ESP_LOGW(TAG, "cannot derive local time from the RTC (TZ='%s')", s_tz);
    }
    s_snapshot.state = SVC_TIME_STATE_RTC_ONLY;
    s_snapshot.valid = true;
    ESP_LOGI(TAG, "RTC time loaded: UTC %04u-%02u-%02u %02u:%02u:%02u",
             (unsigned)rtc_now.year, (unsigned)rtc_now.month, (unsigned)rtc_now.day,
             (unsigned)rtc_now.hours, (unsigned)rtc_now.minutes,
             (unsigned)rtc_now.seconds);
}

esp_err_t svc_time_start(void)
{
    esp_err_t err;
    BaseType_t res;

    if (s_started) {
        return ESP_OK;
    }

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = SVC_TIME_STATE_UNSYNCED;
    s_snapshot.valid = false;

    /* NVS is optional: a device that has never been configured has no "time"
       namespace. The built-in defaults below are the documented behavior. */
    err = nvs_open(NVS_NAMESPACE_TIME, NVS_READWRITE, &s_nvs);
    if (err == ESP_OK) {
        s_nvs_open = true;
    } else {
        s_nvs_open = false;
        ESP_LOGW(TAG, "NVS namespace '%s' unavailable (%s) -- built-in defaults",
                 NVS_NAMESPACE_TIME, esp_err_to_name(err));
    }

    load_timezone_from_nvs();
    load_servers_from_nvs();
    apply_timezone();
    load_initial_time_from_rtc();

    s_mailbox    = xQueueCreate(SVC_TIME_MAILBOX_LEN, sizeof(event_t));
    s_state_lock = xSemaphoreCreateMutex();
    s_sync_lock  = xSemaphoreCreateMutex();
    if (!s_mailbox || !s_state_lock || !s_sync_lock) {
        ESP_LOGE(TAG, "cannot create mailbox/locks (mailbox=%p state=%p sync=%p)",
                 (void *)s_mailbox, (void *)s_state_lock, (void *)s_sync_lock);
        cleanup_time_objects();
        return ESP_FAIL;
    }

    const esp_timer_create_args_t targs = {
        .callback = &backoff_timer_cb,
        .arg      = NULL,
        .name     = "svc_time_backoff",
    };
    err = esp_timer_create(&targs, &s_backoff_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        cleanup_time_objects();
        return ESP_FAIL;
    }

    /* The worker is created before the subscription on purpose: if this
       fails, cleanup_time_objects() can delete the mailbox safely. Subscribing
       first would leave the kernel forwarding events into a deleted queue --
       there is no event_bus_unsubscribe(). Nothing can be missed either way:
       svc_network, the only producer of EVENT_NETWORK_CONNECTED, starts after
       this function returns. */
    res = xTaskCreatePinnedToCore(svc_time_worker, "svc_time",
                                  SVC_TIME_TASK_STACK, NULL,
                                  SVC_TIME_TASK_PRIO, &s_worker,
                                  SVC_TIME_TASK_CORE);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed: res=%ld", (long)res);
        cleanup_time_objects();
        return ESP_FAIL;
    }

    if (!event_bus_subscribe(s_mailbox, EVENT_NETWORK_CONNECTED)) {
        /* Not fatal: the getters, a manual sync and the backoff retry still
           work; only the automatic sync on connect is lost. */
        ESP_LOGE(TAG, "event_bus_subscribe(EVENT_NETWORK_CONNECTED) failed");
    }

    s_started = true;
    ESP_LOGI(TAG, "started: state=%s, %u NTP slots, TZ='%s'",
             state_to_str(s_snapshot.state), (unsigned)SVC_TIME_NTP_SLOTS, s_tz);
    return ESP_OK;
}

esp_err_t svc_time_set_timezone_posix(const char *tz)
{
    esp_err_t err;

    if (!tz || tz[0] == '\0' || strlen(tz) >= SVC_TIME_TZ_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!state_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    strncpy(s_tz, tz, sizeof(s_tz) - 1);
    s_tz[sizeof(s_tz) - 1] = '\0';
    state_unlock();

    /* Apply to the libc first, then re-derive the cached local time so a read
       right after this call sees the new zone. The UTC value is never
       rewritten: the zone and DST stay a display-side concern. */
    apply_timezone();
    if (state_lock()) {
        if (s_snapshot.valid && !utc_to_local(&s_snapshot.local, &s_snapshot.utc)) {
            ESP_LOGW(TAG, "cannot re-derive local time for TZ='%s'", tz);
        }
        state_unlock();
    }

    if (!s_nvs_open) {
        ESP_LOGW(TAG, "TZ '%s' applied but not persisted (NVS unavailable)", tz);
        return ESP_ERR_NVS_NOT_INITIALIZED;
    }
    err = nvs_set_str(s_nvs, NVS_KEY_TZ, tz);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TZ '%s' applied but not persisted: %s",
                 tz, esp_err_to_name(err));
    }
    return err;
}

const char *svc_time_get_timezone_posix(void)
{
    return s_tz;   /* file-static storage, filled by svc_time_start() */
}

esp_err_t svc_time_get_snapshot(svc_time_snapshot_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!state_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    *out = s_snapshot;
    state_unlock();
    return ESP_OK;
}

/* Shared body of _get_local() and _get_utc(): copy one field out of a valid
   snapshot. */
static esp_err_t get_time_field(rtc_time_t *out, bool want_utc)
{
    bool valid;

    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!state_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    valid = s_snapshot.valid;
    if (valid) {
        *out = want_utc ? s_snapshot.utc : s_snapshot.local;
    }
    state_unlock();
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t svc_time_get_local(rtc_time_t *out)
{
    return get_time_field(out, false);
}

esp_err_t svc_time_get_utc(rtc_time_t *out)
{
    return get_time_field(out, true);
}

bool svc_time_is_trustworthy(void)
{
    bool trustworthy = false;

    if (!s_started) {
        return false;
    }
    if (!state_lock()) {
        return false;
    }
    trustworthy = s_snapshot.valid && s_snapshot.state == SVC_TIME_STATE_NTP_SYNCED;
    state_unlock();
    return trustworthy;
}

esp_err_t svc_time_sync_ntp_once(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Blocks for up to SVC_TIME_SYNC_WAIT_MS. Callers must not be the kernel
       event dispatcher task (fan-out would stall) or an esp_timer callback. */
    return run_ntp_sync("manual");
}

esp_err_t svc_time_set_ntp_server(uint8_t slot, const char *host)
{
    char host_copy[SVC_TIME_NTP_HOST_MAX];
    char key[sizeof(NVS_KEY_NTP_PREFIX) + 4];
    esp_err_t err;

    if (slot >= SVC_TIME_NTP_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!host || host[0] == '\0' || strlen(host) >= SVC_TIME_NTP_HOST_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!state_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    strncpy(s_servers[slot], host, SVC_TIME_NTP_HOST_MAX - 1);
    s_servers[slot][SVC_TIME_NTP_HOST_MAX - 1] = '\0';
    memcpy(host_copy, s_servers[slot], sizeof(host_copy));
    state_unlock();

    if (!s_nvs_open) {
        ESP_LOGW(TAG, "NTP slot %u set to '%s' but not persisted (NVS unavailable)",
                 (unsigned)slot, host_copy);
        return ESP_ERR_NVS_NOT_INITIALIZED;
    }
    snprintf(key, sizeof(key), "%s%u", NVS_KEY_NTP_PREFIX, (unsigned)slot);
    err = nvs_set_str(s_nvs, key, host_copy);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NTP slot %u set to '%s' but not persisted: %s",
                 (unsigned)slot, host_copy, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "NTP slot %u set to '%s'", (unsigned)slot, host_copy);
    }
    return err;
}







