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
#include "svc_timetable.h"
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
#define TASK_STACK         8192
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
static volatile bool     s_regenerating        = false;

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

/* The event is a SIGNAL ONLY -- svc_prayer_engine_times_t is 120 bytes,
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

/* ---------------------------------------------------------------------
 * Seasonal jamat computation helpers (5.1a)
 * ------------------------------------------------------------------- */

/* True if (m, d) is in [start, end) with year-wrap. */
static bool date_in_range(uint8_t m, uint8_t d,
                          uint8_t start_m, uint8_t start_d,
                          uint8_t end_m, uint8_t end_d)
{
    uint16_t v = (uint16_t)m * 100u + d;
    uint16_t s = (uint16_t)start_m * 100u + start_d;
    uint16_t e = (uint16_t)end_m   * 100u + end_d;
    if (s <= e) return v >= s && v < e;
    return v >= s || v < e;
}

/* Summer if (m, d) is in [summer_start, winter_start). */
static uint8_t current_season(const season_cfg_t *s, uint8_t m, uint8_t d)
{
    return date_in_range(m, d, s->summer_start_month, s->summer_start_day,
                         s->winter_start_month, s->winter_start_day) ? 1u : 0u;
}

/* Round `sec` to the nearest multiple of `round_min * 60` seconds from
   local midnight. round_min == 0 means "return sec unchanged". */
static uint32_t round_to_min(uint32_t sec, uint8_t round_min)
{
    if (round_min == 0) return sec;
    uint32_t step = (uint32_t)round_min * 60u;
    uint32_t half = step / 2u;
    return ((sec + half) / step) * step;
}

/* Compute one prayer's jamat from its anchor value and cfg. Returns
   UINT32_MAX if the anchor is DISABLED or the target is invalid. */
static uint32_t compute_jamat(uint32_t adhan_sec,
                              uint32_t sunrise_sec,
                              uint32_t midnight_sec,
                              const jamat_season_cfg_t *cfg,
                              uint8_t round_min)
{
    if (cfg->anchor == SVC_CONFIG_JAMAT_DISABLED) return UINT32_MAX;

    uint32_t base;
    uint32_t off_s = (uint32_t)cfg->offset_min * 60u;

    switch (cfg->anchor) {
    case SVC_CONFIG_JAMAT_ADHAN_PLUS:
        base = adhan_sec + off_s;
        break;
    case SVC_CONFIG_JAMAT_SUNRISE_MINUS:
        if (sunrise_sec == UINT32_MAX || sunrise_sec < off_s) return UINT32_MAX;
        base = sunrise_sec - off_s;
        break;
    case SVC_CONFIG_JAMAT_MIDNIGHT_MINUS:
        if (midnight_sec == UINT32_MAX || midnight_sec < off_s) return UINT32_MAX;
        base = midnight_sec - off_s;
        break;
    default:
        return UINT32_MAX;
    }
    return round_to_min(base, round_min);
}

/* Advance (y, m, d) by one calendar day. Uses timegm/gmtime_r so
   month/year rollover and leap years are handled by the libc. Returns
   the input on failure -- caller checks against known-good values. */
static void date_advance_one_day(uint16_t y, uint8_t m, uint8_t d,
                                 uint16_t *oy, uint8_t *om, uint8_t *od)
{
    struct tm t = {0};
    t.tm_year = (int)y - 1900;
    t.tm_mon  = (int)m - 1;
    t.tm_mday = (int)d + 1;
    t.tm_isdst = 0;
    time_t e = timegm(&t);
    struct tm out = {0};
    if (e == (time_t)-1 || gmtime_r(&e, &out) == NULL) {
        *oy = y; *om = m; *od = d;
        return;
    }
    *oy = (uint16_t)(out.tm_year + 1900);
    *om = (uint8_t)(out.tm_mon + 1);
    *od = (uint8_t)out.tm_mday;
}

/* Background task: regenerate the year timetable (~1 s). On success,
   triggers a recalculate which now reads the file. */
static void regen_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "regenerating timetable...");
    esp_err_t err = svc_timetable_generate_full_year();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "regen failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "regen complete, refreshing cache");
        svc_prayer_engine_service_recalculate();
    }
    s_regenerating = false;
    vTaskDelete(NULL);
}

static bool timetable_needs_regen(void)
{
    return svc_timetable_needs_regen_from_config();
}

/* File value if has_file, else fresh value. */
static uint32_t merge_adhan(bool has_file, uint32_t file_val, uint32_t fresh_val)
{
    return has_file ? file_val : fresh_val;
}

/* Validate a jamat against its adhan and the next prayer's time.
   - file_val < adhan_val         : reject (fall back to computed)
   - file_val >= next_val != 0    : accept with warn (mosque authority)
   - else                         : accept silently */
static uint32_t merge_jamat(bool has_file, uint32_t file_val,
                            uint32_t computed_val,
                            uint32_t adhan_val,
                            uint32_t next_val,
                            const char *name)
{
    if (!has_file) return computed_val;
    if (file_val < adhan_val) {
        ESP_LOGW(TAG, "timetable jamat[%s]=%u < adhan=%u -- using computed",
                 name, (unsigned)file_val, (unsigned)adhan_val);
        return computed_val;
    }
    if (next_val != 0 && file_val >= next_val) {
        ESP_LOGW(TAG, "timetable jamat[%s]=%u >= next=%u -- accepting (mosque authority)",
                 name, (unsigned)file_val, (unsigned)next_val);
    }
    return file_val;
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

    /* Year guard: never publish prayer times for an implausible year. */
    if (snap.rtc_untrusted) {
        ESP_LOGW(TAG, "time is untrusted (OSF set or corrupt year) -- refusing to publish");
        return ESP_ERR_INVALID_STATE;
    }

    if (snap.local.year < 2020 || snap.local.year > 2100) {
        ESP_LOGW(TAG, "year %u outside [2020,2100] -- refusing to compute",
                 (unsigned)snap.local.year);
        return ESP_ERR_INVALID_STATE;
    }

    /* Timetable awareness. */
    if (s_started) {
        svc_timetable_set_current_date(snap.local.year, snap.local.month, snap.local.day);
        if (!s_regenerating && timetable_needs_regen()) {
            s_regenerating = true;
            if (xTaskCreate(regen_task, "tt_regen", 8192, NULL,
                            tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
                ESP_LOGW(TAG, "failed to spawn regen task");
                s_regenerating = false;
            }
        }
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
        method, madhab, highlat, cfg.custom_fajr_angle, cfg.custom_isha_angle, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "calculate failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Build result. Fresh values serve as fallback; timetable overrides
       per field when present. */
    svc_prayer_engine_times_t result = {0};
    result.date_yyyymmdd = (uint32_t)snap.local.year  * 10000U
                         + (uint32_t)snap.local.month * 100U
                         + (uint32_t)snap.local.day;
    result.tz_offset_minutes = tz_offset_min;

    svc_timetable_day_t tt_today = {0}, tt_tomorrow = {0};
    svc_timetable_get_today(&tt_today);
    svc_timetable_get_tomorrow(&tt_tomorrow);

    /* Fresh adhan values (for fallback and for the tripwire). */
    uint32_t fresh_adhan[5] = {
        raw.fajr_sec    + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_FAJR]    * 60U,
        raw.dhuhr_sec   + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_DHUHR]   * 60U,
        raw.asr_sec     + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_ASR]     * 60U,
        raw.maghrib_sec + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_MAGHRIB] * 60U,
        raw.isha_sec    + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_ISHA]    * 60U,
    };

    /* Astronomical tier (always fresh). */
    result.astronomical_fajr_sec    = raw.fajr_sec;
    result.astronomical_dhuhr_sec   = raw.dhuhr_sec;
    result.astronomical_asr_sec     = raw.asr_sec;
    result.astronomical_maghrib_sec = raw.maghrib_sec;
    result.astronomical_isha_sec    = raw.isha_sec;

    /* Merge adhan tier. */
    result.fajr_sec    = merge_adhan(tt_today.has_fajr,    tt_today.fajr,    fresh_adhan[0]);
    result.dhuhr_sec   = merge_adhan(tt_today.has_dhuhr,   tt_today.dhuhr,   fresh_adhan[1]);
    result.asr_sec     = merge_adhan(tt_today.has_asr,     tt_today.asr,     fresh_adhan[2]);
    result.maghrib_sec = merge_adhan(tt_today.has_maghrib, tt_today.maghrib, fresh_adhan[3]);
    result.isha_sec    = merge_adhan(tt_today.has_isha,    tt_today.isha,    fresh_adhan[4]);

    /* Auxiliary. */
    uint32_t sunrise_used  = merge_adhan(tt_today.has_sunrise,     tt_today.sunrise,     raw.sunrise_sec);
    uint32_t midnight_used = merge_adhan(tt_today.has_midnight,    tt_today.midnight,    raw.midnight_sec);
    result.sunrise_sec    = sunrise_used;
    result.sunset_sec     = raw.sunset_sec;
    result.asr_yellow_sec = merge_adhan(tt_today.has_asr_yellow,   tt_today.asr_yellow,  raw.asr_yellow_sec);
    result.midnight_sec   = midnight_used;

    uint32_t zawal_off = (uint32_t)cfg.zawal_buffer_min * 60U;
    uint32_t fresh_zawal = (raw.dhuhr_sec > zawal_off) ? (raw.dhuhr_sec - zawal_off) : 0U;
    result.zawal_start_sec = merge_adhan(tt_today.has_zawal_start, tt_today.zawal_start, fresh_zawal);

    result.sehri_end_sec = merge_adhan(tt_today.has_sehri_end, tt_today.sehri_end, raw.fajr_sec);
    result.iftar_sec     = merge_adhan(tt_today.has_iftar,     tt_today.iftar,     raw.sunset_sec);

    /* Source tag. */
    result.source = (uint8_t)svc_timetable_get_source();

    /* Tripwire: compare file values against fresh; >60 s mismatch uses fresh. */
    #define TT_TRIPWIRE(idx, field, name) do { \
        if (tt_today.has_##field) { \
            uint32_t _d = (tt_today.field > fresh_adhan[idx]) \
                        ? (tt_today.field - fresh_adhan[idx]) \
                        : (fresh_adhan[idx] - tt_today.field); \
            if (_d > 60U) { \
                ESP_LOGW(TAG, "timetable[%s]=%u differs from fresh=%u by %u s -- using fresh", \
                         name, (unsigned)tt_today.field, (unsigned)fresh_adhan[idx], (unsigned)_d); \
                result.field##_sec = fresh_adhan[idx]; \
            } \
        } \
    } while (0)

    TT_TRIPWIRE(0, fajr,    "fajr");
    TT_TRIPWIRE(1, dhuhr,   "dhuhr");
    TT_TRIPWIRE(2, asr,     "asr");
    TT_TRIPWIRE(3, maghrib, "maghrib");
    TT_TRIPWIRE(4, isha,    "isha");
    #undef TT_TRIPWIRE

    /* Tier 3: jamat (today), seasonal. Winter/summer decision uses the
       target date. */
    uint8_t season = current_season(&cfg.season,
                                    (uint8_t)snap.local.month,
                                    (uint8_t)snap.local.day);
    result.season = season;

    uint32_t adhan_secs[SVC_CONFIG_PRAYER_COUNT] = {
        result.fajr_sec, result.dhuhr_sec, result.asr_sec,
        result.maghrib_sec, result.isha_sec
    };
    static const char *const s_names[SVC_CONFIG_PRAYER_COUNT] = {
        "fajr","dhuhr","asr","maghrib","isha"
    };
    /* Next prayer's time for bracket validation:
       Fajr ends at sunrise; Dhuhr at Asr; Asr at Maghrib; Maghrib at Isha;
       Isha at Islamic midnight. */
    uint32_t next_sec[SVC_CONFIG_PRAYER_COUNT] = {
        sunrise_used,
        adhan_secs[2],
        adhan_secs[3],
        adhan_secs[4],
        midnight_used,
    };
    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        const jamat_season_cfg_t *jc = (season == 0)
            ? &cfg.jamat[p].winter : &cfg.jamat[p].summer;
        uint32_t computed = compute_jamat(
            adhan_secs[p],
            sunrise_used,           /* merged sunrise */
            midnight_used,          /* merged midnight */
            jc,
            cfg.jamat[p].round_min);
        result.jamat_sec[p] = merge_jamat(
            tt_today.has_jamat[p],
            tt_today.jamat[p],
            computed,
            adhan_secs[p],
            next_sec[p],
            s_names[p]);
    }

    /* Tier 3: jamat (tomorrow). Prefer file, fall back to fresh + computed. */
    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        result.jamat_tomorrow_sec[p] = UINT32_MAX;
    }
    {
        uint16_t tm_y; uint8_t tm_m, tm_d;
        date_advance_one_day((uint16_t)snap.local.year,
                             (uint8_t)snap.local.month,
                             (uint8_t)snap.local.day,
                             &tm_y, &tm_m, &tm_d);
        int16_t tm_tz_off_min = tz_offset_minutes_at_noon(tm_y, tm_m, tm_d);
        double  tm_tz_off     = (double)tm_tz_off_min / 60.0;

        svc_prayer_times_t tm_raw = {0};
        esp_err_t tm_err = svc_prayer_engine_calculate(
            (int)tm_y, (int)tm_m, (int)tm_d,
            cfg.latitude, cfg.longitude, tm_tz_off,
            method, madhab, highlat, cfg.custom_fajr_angle, cfg.custom_isha_angle, &tm_raw);
        if (tm_err == ESP_OK) {
            uint8_t tm_season = current_season(&cfg.season, tm_m, tm_d);
            uint32_t tm_adhan[SVC_CONFIG_PRAYER_COUNT] = {
                tm_raw.fajr_sec    + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_FAJR]    * 60u,
                tm_raw.dhuhr_sec   + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_DHUHR]   * 60u,
                tm_raw.asr_sec     + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_ASR]     * 60u,
                tm_raw.maghrib_sec + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_MAGHRIB] * 60u,
                tm_raw.isha_sec    + (uint32_t)cfg.adhan_offset_min[SVC_CONFIG_PRAYER_ISHA]    * 60u,
            };
            uint32_t tm_sunrise_used  = merge_adhan(tt_tomorrow.has_sunrise,  tt_tomorrow.sunrise,  tm_raw.sunrise_sec);
            uint32_t tm_midnight_used = merge_adhan(tt_tomorrow.has_midnight, tt_tomorrow.midnight, tm_raw.midnight_sec);
            uint32_t tm_next[SVC_CONFIG_PRAYER_COUNT] = {
                tm_sunrise_used, tm_adhan[2], tm_adhan[3], tm_adhan[4], tm_midnight_used,
            };
            for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
                const jamat_season_cfg_t *jc = (tm_season == 0)
                    ? &cfg.jamat[p].winter : &cfg.jamat[p].summer;
                uint32_t computed = compute_jamat(
                    tm_adhan[p],
                    tm_sunrise_used,
                    tm_midnight_used,
                    jc,
                    cfg.jamat[p].round_min);
                result.jamat_tomorrow_sec[p] = merge_jamat(
                    tt_tomorrow.has_jamat[p],
                    tt_tomorrow.jamat[p],
                    computed,
                    tm_adhan[p],
                    tm_next[p],
                    s_names[p]);
            }
        }
    }

    result.valid = true;

    if (!lock_take()) return ESP_ERR_TIMEOUT;
    s_cache = result;
    s_cache_valid = true;
    lock_give();

    publish_times_updated(&result);

    ESP_LOGI(TAG, "prayer times updated (date=%lu offset=%d min season=%s): "
                  "fajr=%u dhuhr=%u asr=%u maghrib=%u isha=%u "
                  "j_fajr=%u j_dhuhr=%u j_asr=%u j_maghrib=%u j_isha=%u",
             (unsigned long)result.date_yyyymmdd,
             (int)result.tz_offset_minutes,
             result.season ? "summer" : "winter",
             (unsigned)result.fajr_sec, (unsigned)result.dhuhr_sec,
             (unsigned)result.asr_sec, (unsigned)result.maghrib_sec,
             (unsigned)result.isha_sec,
             (unsigned)result.jamat_sec[0], (unsigned)result.jamat_sec[1],
             (unsigned)result.jamat_sec[2], (unsigned)result.jamat_sec[3],
             (unsigned)result.jamat_sec[4]);

    static const char *const s_prayer_names[SVC_CONFIG_PRAYER_COUNT] = {
        "Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"
    };
    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        if (result.jamat_sec[p] != UINT32_MAX
            && result.jamat_tomorrow_sec[p] != UINT32_MAX
            && result.jamat_sec[p] != result.jamat_tomorrow_sec[p]) {
            ESP_LOGI(TAG, "jamat change tomorrow: %s %02u:%02u -> %02u:%02u",
                     s_prayer_names[p],
                     (unsigned)((result.jamat_sec[p] % 86400u) / 3600u),
                     (unsigned)(((result.jamat_sec[p] % 86400u) % 3600u) / 60u),
                     (unsigned)((result.jamat_tomorrow_sec[p] % 86400u) / 3600u),
                     (unsigned)(((result.jamat_tomorrow_sec[p] % 86400u) % 3600u) / 60u));
        }
    }
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

bool svc_prayer_engine_service_is_regenerating(void)
{
    return s_regenerating;
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
