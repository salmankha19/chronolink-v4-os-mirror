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
static bool              s_cache_valid = false;

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

/* The event is a SIGNAL ONLY -- svc_prayer_engine_times_t is 48 bytes,
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

/* Compute timezone offset in hours (local minus UTC) from a svc_time snapshot.
   Handles cross-midnight wraps that stay within +/-14 h (real timezones). */
static double tz_offset_hours(const svc_time_snapshot_t *snap)
{
    int utc_secs   = snap->utc.hours   * 3600 + snap->utc.minutes   * 60 + snap->utc.seconds;
    int local_secs = snap->local.hours * 3600 + snap->local.minutes * 60 + snap->local.seconds;
    int diff = local_secs - utc_secs;
    if (diff >  14 * 3600) diff -= 24 * 3600;
    if (diff < -14 * 3600) diff += 24 * 3600;
    return (double)diff / 3600.0;
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

    /* Compute offset from svc_time utc vs local fields. */
    double tz_off = tz_offset_hours(&snap);

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

    ESP_LOGI(TAG, "prayer times updated: fajr=%u dhuhr=%u asr=%u maghrib=%u isha=%u",
             (unsigned)result.fajr_sec, (unsigned)result.dhuhr_sec,
             (unsigned)result.asr_sec, (unsigned)result.maghrib_sec,
             (unsigned)result.isha_sec);
    return ESP_OK;
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

/* Worker task: drains the mailbox. Each EVENT_TIME_UPDATE triggers a
   recalculation; other event types are ignored. Runs on core 0. */
static void prayer_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task started");

    event_t evt;
    for (;;) {
        if (xQueueReceive(s_mailbox, &evt, portMAX_DELAY) == pdTRUE) {
            if (evt.type == EVENT_TIME_UPDATE) {
                ESP_LOGD(TAG, "EVENT_TIME_UPDATE -- recalculating");
                svc_prayer_engine_service_recalculate();
            }
        }
    }
}
