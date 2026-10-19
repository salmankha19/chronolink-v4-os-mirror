/*
 * svc_prayer_engine_service.c
 *
 * ChronoLink V4 OS -- Prayer engine runtime service (implementation).
 *
 * Subscribes to EVENT_TIME_UPDATE (published on successful NTP sync)
 * and recalculates. Also publishes EVENT_PRAYER_TIMES_UPDATED so
 * consumers (ui_producer, svc_audio, future runtime) can react.
 */
#include "svc_prayer_engine_service.h"
#include "svc_config.h"
#include "svc_time.h"
#include "svc_prayer_engine.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>     /* struct tm, mktime, timegm, time_t (noon-offset calc) */

static const char *TAG = "svc_prayer_eng_svc";

#define MAILBOX_LEN        4
#define TASK_STACK         4096
#define TASK_PRIO          (tskIDLE_PRIORITY + 2)
#define TASK_CORE          0
#define LOCK_TIMEOUT_MS    1000

static bool              s_started = false;
static QueueHandle_t     s_mailbox = NULL;
static TaskHandle_t      s_task    = NULL;
static SemaphoreHandle_t s_lock    = NULL;
static svc_prayer_engine_times_t s_cache;
static bool              s_cache_valid         = false;
static bool              s_date_changed_pending = false;

static void prayer_task(void *arg);

static bool lock_take(void)
{
    if (!s_lock) return false;
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void lock_give(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* Snapshot the current cache key (date + noon offset). Returns false if the
   cache has never been valid. Takes the lock so callers do not read
   s_cache fields that the worker or a manual recalculate could be writing. */
static bool cache_get_key(uint32_t *date, int16_t *offset)
{
    bool ok;

    if (!lock_take()) return false;
    ok = s_cache_valid;
    if (ok) {
        *date   = s_cache.date_yyyymmdd;
        *offset = s_cache.tz_offset_minutes;
    }
    lock_give();
    return ok;
}

/* The event is a SIGNAL ONLY -- svc_prayer_engine_times_t is 52 bytes,
   event_t.data is 32 bytes. Consumers must call
   svc_prayer_engine_service_get_times() to read the full snapshot. */
static void publish_times_updated(const svc_prayer_engine_times_t *t)
{
    (void)t;
    event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = EVENT_PRAYER_TIMES_UPDATED;
    event_bus_publish(&evt);
}

esp_err_t svc_prayer_engine_service_start(void)
{
    if (s_started) return ESP_OK;

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "Failed to create lock");
        return ESP_FAIL;
    }

    s_mailbox = xQueueCreate(MAILBOX_LEN, sizeof(event_t));
    if (!s_mailbox) {
        ESP_LOGE(TAG, "Failed to create mailbox");
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_FAIL;
    }

    BaseType_t res = xTaskCreatePinnedToCore(
        prayer_task,
        "prayer_svc",
        TASK_STACK,
        NULL,
        TASK_PRIO,
        &s_task,
        TASK_CORE);

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        vQueueDelete(s_mailbox);
        s_mailbox = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_FAIL;
    }

    if (!event_bus_subscribe(s_mailbox, EVENT_TIME_UPDATE)) {
        ESP_LOGW(TAG, "Failed to subscribe to EVENT_TIME_UPDATE -- no recalculation on NTP sync");
    }
    if (!event_bus_subscribe(s_mailbox, EVENT_TIME_DATE_CHANGED)) {
        ESP_LOGW(TAG, "Failed to subscribe to EVENT_TIME_DATE_CHANGED -- no recalculation on date change");
    }

    s_started = true;

    /* Initial calculation. The worker task will also react to
       EVENT_TIME_UPDATE (published on NTP sync). Calling it here ensures
       the cache is warm before start() returns, so a caller that
       immediately calls get_times() sees data instead of
       ESP_ERR_INVALID_STATE. Failure is non-fatal: config may be unset
       at boot (location_valid == false). */
    esp_err_t err = svc_prayer_engine_service_recalculate();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "initial calculation failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

/* Compute the local-UTC offset (minutes) at LOCAL NOON of the target date.
   Why noon: prayer times for a given date should all use the same offset,
   even on a DST-transition day where local 01:00 and local 15:00 have
   different offsets. Noon is safely past any 01:00-03:00 transition.
   Uses the current TZ env var (set by svc_time at start), which encodes
   the POSIX DST rule. Returns 0 on failure (mktime/timegm rejected tm).
   The result is a signed int16 covering all real timezones (+/-840 min). */
static int16_t tz_offset_minutes_at_noon(int year, int month, int day)
{
    struct tm tm_local = {0};
    struct tm tm_utc   = {0};

    tm_local.tm_year = tm_utc.tm_year = year - 1900;
    tm_local.tm_mon  = tm_utc.tm_mon  = month - 1;
    tm_local.tm_mday = tm_utc.tm_mday = day;
    tm_local.tm_hour = tm_utc.tm_hour = 12;  /* noon */
    tm_local.tm_isdst = -1;   /* let mktime apply the POSIX TZ DST rule */
    tm_utc.tm_isdst   = 0;    /* timegm ignores DST (it is UTC) */

    time_t epoch_local = mktime(&tm_local);
    time_t epoch_utc   = timegm(&tm_utc);

    if (epoch_local == (time_t)-1 || epoch_utc == (time_t)-1) {
        ESP_LOGW(TAG, "tz offset at noon: mktime/timegm failed");
        return 0;
    }

    /* For BST (+1h): local noon = 11:00 UTC -> epoch_local < epoch_utc.
       offset_sec = epoch_utc - epoch_local = +3600. */
    int32_t offset_sec = (int32_t)(epoch_utc - epoch_local);
    /* Defensive: reject implausible offsets (>14h) as a sign of a broken TZ. */
    if (offset_sec >  14 * 3600 || offset_sec < -14 * 3600) {
        ESP_LOGW(TAG, "tz offset at noon: implausible %ld sec, using 0",
                 (long)offset_sec);
        return 0;
    }
    return (int16_t)(offset_sec / 60);
}

esp_err_t svc_prayer_engine_service_recalculate(void)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;

    /* Read config. */
    svc_config_t cfg;
    esp_err_t err = svc_config_get(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "config not available: %s", esp_err_to_name(err));
        return err;
    }
    if (!cfg.location_valid) {
        ESP_LOGW(TAG, "location not set -- cannot calculate prayer times");
        return ESP_ERR_INVALID_STATE;
    }

    /* Read time. */
    svc_time_snapshot_t snap;
    err = svc_time_get_snapshot(&snap);
    if (err != ESP_OK || !snap.valid) {
        ESP_LOGW(TAG, "time not yet valid -- cannot calculate prayer times");
        return ESP_ERR_INVALID_STATE;
    }

    /* Offset at local noon of the target date (DST-safe). */
    int16_t tz_offset_min = tz_offset_minutes_at_noon(
        (int)snap.local.year, (int)snap.local.month, (int)snap.local.day);
    double tz_off = (double)tz_offset_min / 60.0;

    /* Cast config enums to prayer-engine enums (values 0..N match). */
    svc_prayer_method_t method   = (svc_prayer_method_t)cfg.method;
    svc_prayer_madhab_t madhab   = (svc_prayer_madhab_t)cfg.madhab;
    svc_prayer_highlat_t highlat = (svc_prayer_highlat_t)cfg.high_lat_rule;

    svc_prayer_times_t raw;
    err = svc_prayer_engine_calculate(
        (int)snap.local.year, (int)snap.local.month, (int)snap.local.day,
        cfg.latitude, cfg.longitude, tz_off,
        method, madhab, highlat, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "calculate failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Apply per-prayer adhan offsets and copy the rest. */
    svc_prayer_engine_times_t result = {0};
    result.date_yyyymmdd = (uint32_t)snap.local.year  * 10000U
                         + (uint32_t)snap.local.month * 100U
                         + (uint32_t)snap.local.day;

    result.tz_offset_minutes = tz_offset_min;

    result.sunrise_sec    = raw.sunrise_sec;
    result.sunset_sec     = raw.sunset_sec;
    result.asr_yellow_sec = raw.asr_yellow_sec;
    result.midnight_sec   = raw.midnight_sec;

    result.fajr_sec    = raw.fajr_sec    + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_FAJR]    * 60U;
    result.dhuhr_sec   = raw.dhuhr_sec   + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_DHUHR]   * 60U;
    result.asr_sec     = raw.asr_sec     + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_ASR]     * 60U;
    result.maghrib_sec = raw.maghrib_sec + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_MAGHRIB] * 60U;
    result.isha_sec    = raw.isha_sec    + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_ISHA]    * 60U;

    uint32_t zawal_off = (uint32_t)cfg.zawal_buffer_min * 60U;
    result.zawal_start_sec = (raw.dhuhr_sec > zawal_off)
                           ? (raw.dhuhr_sec - zawal_off) : 0U;

    result.valid = true;

    if (!lock_take()) return ESP_ERR_TIMEOUT;
    s_cache = result;
    s_cache_valid = true;
    lock_give();

    publish_times_updated(&result);

    ESP_LOGI(TAG, "prayer times updated (date=%lu offset=%d min): "
                  "fajr=%u dhuhr=%u asr=%u maghrib=%u isha=%u",
             (unsigned long)result.date_yyyymmdd,
             (int)result.tz_offset_minutes,
             (unsigned)result.fajr_sec, (unsigned)result.dhuhr_sec,
             (unsigned)result.asr_sec, (unsigned)result.maghrib_sec,
             (unsigned)result.isha_sec);
    return ESP_OK;
}

/* Return true if the cache needs to be recomputed because the current
   local date or the effective UTC offset has diverged from what the
   cache was computed for. If cache is empty, returns true (must recalc).
   Reads svc_time; returns true on any error reading time (fail-safe:
   prefer to recalculate rather than serve stale data). */
static bool cache_is_stale(void)
{
    svc_time_snapshot_t snap;
    if (svc_time_get_snapshot(&snap) != ESP_OK || !snap.valid) {
        return true;   /* can't tell -- prefer recalc */
    }

    uint32_t cur_date = (uint32_t)snap.local.year  * 10000U
                      + (uint32_t)snap.local.month * 100U
                      + (uint32_t)snap.local.day;
    int16_t cur_offset = tz_offset_minutes_at_noon(
        (int)snap.local.year, (int)snap.local.month, (int)snap.local.day);

    if (!lock_take()) return true;
    bool stale = (!s_cache_valid)
              || (s_cache.date_yyyymmdd     != cur_date)
              || (s_cache.tz_offset_minutes != cur_offset);
    lock_give();
    return stale;
}

esp_err_t svc_prayer_engine_service_get_times(svc_prayer_engine_times_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_started) return ESP_ERR_INVALID_STATE;

    if (!lock_take()) return ESP_ERR_TIMEOUT;
    bool valid = s_cache_valid;
    if (valid) *out = s_cache;
    lock_give();

    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/* Worker task: drains the mailbox. Recalculates on EVENT_TIME_UPDATE,
   on EVENT_TIME_DATE_CHANGED, and on a 60 s timeout whenever the cache
   key (local date / noon UTC offset) has drifted -- self-heals a dropped
   event. Runs on core 0. */
static void prayer_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task started");

    /* One-shot baseline stack high-water (F15). */
    {
        UBaseType_t hw = uxTaskGetStackHighWaterMark2(NULL);
        ESP_LOGI(TAG, "task stack high-water (baseline): %u bytes free of %d",
                 (unsigned)hw, TASK_STACK);
    }

    event_t evt;
    for (;;) {
        /* Wake on any event, or every 60 s as a self-healing fallback if
           an event is dropped by the event bus. 60 s also bounds the
           00:00->00:01 delay for the midnight rollover: at most one extra
           tick before the recalculation runs. */
        BaseType_t got = xQueueReceive(s_mailbox, &evt, pdMS_TO_TICKS(60000));

        bool do_recalc     = false;
        bool is_date_event = false;

        if (got == pdTRUE) {
            if (evt.type == EVENT_TIME_UPDATE) {
                /* NTP sync: authoritative time. Always recalc now; a
                   pending midnight deferral is superseded. */
                ESP_LOGD(TAG, "EVENT_TIME_UPDATE received");
                s_date_changed_pending = false;
                do_recalc = true;
            } else if (evt.type == EVENT_TIME_DATE_CHANGED) {
                /* Could be a midnight rollover OR a DST/offset change.
                   Classify below from the current snapshot. */
                ESP_LOGI(TAG, "EVENT_TIME_DATE_CHANGED received");
                is_date_event = true;
            }
        }

        /* Only the time-dependent logic runs when we have a valid time. */
        svc_time_snapshot_t snap;
        if (svc_time_get_snapshot(&snap) == ESP_OK && snap.valid) {
            uint32_t cur_date = (uint32_t)snap.local.year  * 10000U
                              + (uint32_t)snap.local.month *   100U
                              + (uint32_t)snap.local.day;
            int16_t  cur_offset = tz_offset_minutes_at_noon(
                (int)snap.local.year, (int)snap.local.month, (int)snap.local.day);

            uint32_t cached_date   = 0;
            int16_t  cached_offset = 0;
            bool cache_ok = cache_get_key(&cached_date, &cached_offset);

            bool date_differs   = cache_ok && (cached_date   != cur_date);
            bool offset_differs = cache_ok && (cached_offset != cur_offset);

            if (offset_differs) {
                /* DST transition or timezone change: recalculate
                   immediately. The "delay until 00:01" rule is only for
                   a pure date rollover. */
                ESP_LOGI(TAG, "UTC offset changed (%d -> %d min), recalculating now",
                         (int)cached_offset, (int)cur_offset);
                s_date_changed_pending = false;
                do_recalc = true;
            } else if (date_differs) {
                /* Pure date rollover. At 00:00 defer; past 00:00 recalc now
                   (handles a missed event, a late tick, or a time jump). */
                if (snap.local.hours == 0 && snap.local.minutes == 0) {
                    if (!s_date_changed_pending) {
                        ESP_LOGI(TAG, "date rollover detected, deferring recalc until 00:01");
                    }
                    s_date_changed_pending = true;
                } else {
                    s_date_changed_pending = false;
                    do_recalc = true;
                }
            } else if (is_date_event) {
                /* Event fired but the cache key did not change. Nothing to
                   recalculate. */
                ESP_LOGD(TAG, "EVENT_TIME_DATE_CHANGED but cache key unchanged, ignoring");
            }

            /* Pending flag matures the moment the clock leaves 00:00. */
            if (s_date_changed_pending &&
                !(snap.local.hours == 0 && snap.local.minutes == 0)) {
                ESP_LOGI(TAG, "00:01 reached, calculating new day's prayer times");
                s_date_changed_pending = false;
                do_recalc = true;
            }

            /* 60 s self-heal: recalc on any other staleness, but never
               while a midnight deferral is deliberately in progress. */
            if (!do_recalc && !s_date_changed_pending && cache_is_stale()) {
                do_recalc = true;
            }
        }

        if (do_recalc) {
            ESP_LOGD(TAG, "recalculating (pending=%d)", (int)s_date_changed_pending);
            svc_prayer_engine_service_recalculate();

            /* One-shot post-recalculate stack high-water (F15). */
            static bool s_stack_post_logged = false;
            if (!s_stack_post_logged) {
                UBaseType_t hw = uxTaskGetStackHighWaterMark2(NULL);
                ESP_LOGI(TAG, "task stack high-water (post-recalc): %u bytes free of %d",
                         (unsigned)hw, TASK_STACK);
                s_stack_post_logged = true;
            }
        }
    }
}
