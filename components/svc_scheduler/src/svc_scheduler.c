/*
 * svc_scheduler.c
 *
 * ChronoLink V4 OS -- prayer time scheduler (v1: 1 Hz poller).
 *
 * This is the only file in the component with ESP-IDF dependencies. It owns
 * the task, the event-bus mailbox and the adapter from
 * svc_prayer_engine_times_t; every decision is taken in scheduler_logic.c,
 * which stays pure C so the DST behaviour can be unit-tested on a host.
 *
 * Known limitation (docs/STATUS.md, ROADMAP Phase 7): tz_offset_minutes is
 * the offset at NOON of the target date, so prayers that fall before a DST
 * transition on the transition day itself can fire up to one hour early or
 * late. Unlike a (hour, minute) comparison this cannot double-fire.
 */
#include "svc_scheduler.h"

#include "scheduler_logic.h"
#include "svc_time.h"
#include "svc_prayer_engine_service.h"
#include "event_bus.h"
#include "time_events.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>
#include <time.h>

static const char *TAG = "svc_scheduler";

#define SCHEDULER_TICK_MS    1000
#define SCHEDULER_TASK_STACK 4096
#define SCHEDULER_TASK_PRIO  (tskIDLE_PRIORITY + 2)
#define SCHEDULER_TASK_CORE  0
#define MAILBOX_LEN          4

static const char *const kPrayerNames[SCHEDULER_PRAYER_COUNT] = {
    "Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"
};

static bool              s_inited  = false;
static bool              s_started = false;
static volatile bool     s_running = false;
static QueueHandle_t     s_mailbox = NULL;
static TaskHandle_t      s_task    = NULL;

/* Touched only by the scheduler task once start() has returned (init() and
   start() run in the caller's task before the scheduler task exists), so no
   lock is needed: there is never more than one writer. */
static scheduler_state_t s_state;

/* Last scheduler_day_t reset_state_from_service() adapted. Kept so a later
   date rollover can recover yesterday's past-midnight Isha before state is
   cleared. scheduler_tick() does NOT touch this (it uses a fresh local);
   only reset_state_from_service() reads and writes it. */
static scheduler_day_t s_current_day;

static void scheduler_task(void *arg);

/* C2 adapter: svc_prayer_engine_times_t -> scheduler_day_t, ADHAN tier only.
   Jamat firing is out of scope for v1 (prayer_kind_t.PRAYER_KIND_JAMAT is
   reserved), so jamat_sec[] is deliberately ignored here.
   The caller stores its output in s_current_day for reset_state_from_service()
   to consult at the next date rollover; scheduler_tick() uses a fresh local. */
static void adapt_times(const svc_prayer_engine_times_t *times,
                        scheduler_day_t *day)
{
    memset(day, 0, sizeof(*day));
    day->sec[0]            = times->fajr_sec;
    day->sec[1]            = times->dhuhr_sec;
    day->sec[2]            = times->asr_sec;
    day->sec[3]            = times->maghrib_sec;
    day->sec[4]            = times->isha_sec;
    day->date_yyyymmdd     = times->date_yyyymmdd;
    day->tz_offset_minutes = times->tz_offset_minutes;
    day->valid             = times->valid;
}

/* Re-fetch (time, prayer times), preserve any pending Isha from the day
   we are leaving, drop the fired bookkeeping, and re-mark what has already
   passed today. Publishes nothing: this is the response to
   EVENT_TIME_DATE_CHANGED / EVENT_PRAYER_TIMES_UPDATED and the boot path, and
   firing a burst of catch-up adhans there would be wrong. */
static void reset_state_from_service(void)
{
    time_t now = 0;
    svc_prayer_engine_times_t times;
    scheduler_day_t new_day;
    int64_t pending_isha;
    scheduler_state_t prev;

    /* #136 (c): the two early-return error paths below must NOT drop
       last_fired_epoch[]. A transient get_epoch() failure (e.g. mid-NTP
       step) would otherwise wipe the fired markers for prayers that
       already fired this day, letting them re-fire on the next tick once
       time is valid again. Fetch first; only clear s_state once we know we
       have fresh inputs to rebuild it from. */

    if (svc_time_get_epoch(&now) != ESP_OK) {
        ESP_LOGD(TAG, "no valid time yet -- state preserved");
        return;
    }

    esp_err_t err = svc_prayer_engine_service_get_times(&times);
    if (err != ESP_OK || !times.valid) {
        /* Expected during boot: svc_config may have no location yet, or
           svc_time has no trusted time. DEBUG on purpose (A1) -- the next
           tick retries. State is preserved so a fired marker from an
           earlier call is not lost. */
        ESP_LOGD(TAG, "no prayer times yet (%s) -- state preserved",
                 err == ESP_OK ? "not valid" : esp_err_to_name(err));
        return;
    }

    /* Now that we have fresh inputs, take the snapshot of what we are
       leaving, clear the state, and rebuild from the new_day. */
    prev = s_state;
    pending_isha = s_state.pending_isha_epoch;
    memset(&s_state, 0, sizeof(s_state));
    s_state.pending_isha_epoch = pending_isha;

    adapt_times(&times, &new_day);

    /* Preserve yesterday's past-midnight Isha, but only on a true date
       change. A same-date re-fetch (config change) must not stash today's
       Isha into pending_isha_epoch: the live path would double-fire once
       the state re-armed. sec[4] > 86400 == Isha falls after local 24:00. */
    if (pending_isha == 0
        && s_current_day.valid
        && s_current_day.date_yyyymmdd != new_day.date_yyyymmdd
        && s_current_day.sec[SCHEDULER_PRAYER_COUNT - 1] > 86400u
        && s_current_day.sec[SCHEDULER_PRAYER_COUNT - 1] != UINT32_MAX) {
        int64_t old_isha = scheduler_prayer_epoch(
            s_current_day.date_yyyymmdd,
            s_current_day.tz_offset_minutes,
            s_current_day.sec[SCHEDULER_PRAYER_COUNT - 1]);
        if (old_isha > (int64_t)now) {
            pending_isha = old_isha;
        }
    }

    s_current_day = new_day;
    /* Rebuild: premark the past, keep pending Isha, and carry over the markers
       of prayers that fired a moment ago -- the premark grace window would
       otherwise let a prayer that fired up to 2 s ago fire a second time. */
    scheduler_rearm((int64_t)now, &new_day, &prev, pending_isha, &s_state);
}

/* One poll: fetch now + today's times, fire whatever is due. */
static void scheduler_tick(void)
{
    time_t now = 0;
    svc_prayer_engine_times_t times;
    scheduler_day_t day;
    uint8_t fired[SCHEDULER_PRAYER_COUNT];

    if (svc_time_get_epoch(&now) != ESP_OK) {
        return;   /* no valid time yet: nothing can be scheduled */
    }

    esp_err_t err = svc_prayer_engine_service_get_times(&times);
    if (err != ESP_OK || !times.valid) {
        ESP_LOGD(TAG, "get_times: %s",
                 err == ESP_OK ? "not valid" : esp_err_to_name(err));
        return;
    }

    adapt_times(&times, &day);

    int n = scheduler_evaluate((int64_t)now, &day, &s_state, fired);
    for (int i = 0; i < n; i++) {
        uint8_t id = fired[i];
        prayer_time_event_t ev;
        event_t out;

        if (id >= SCHEDULER_PRAYER_COUNT) {
            continue;   /* defensive: evaluate() cannot produce this */
        }

        memset(&ev, 0, sizeof(ev));
        ev.prayer_id       = id;
        ev.kind            = (uint8_t)PRAYER_KIND_ADHAN;
        ev.scheduled_epoch = s_state.last_fired_epoch[id];

        memset(&out, 0, sizeof(out));
        event_init_payload(&out, EVENT_PRAYER_TIME_REACHED, &ev, sizeof(ev));
        event_bus_publish(&out);   /* void: a full bus drops the copy */

        ESP_LOGI(TAG, "%s adhan due (epoch %lld, now %lld)",
                 kPrayerNames[id], (long long)ev.scheduled_epoch,
                 (long long)now);
    }
}

static void scheduler_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task started (1 Hz epoch poll, core %d)",
             SCHEDULER_TASK_CORE);

    TickType_t last_wake = xTaskGetTickCount();

    while (s_running) {
        event_t evt;

        /* Drain the mailbox first. Both subscribed types mean "the day or
           the times changed" -> rebuild the state. Non-blocking, so bus
           traffic never stretches the poll interval. */
        while (xQueueReceive(s_mailbox, &evt, 0) == pdTRUE) {
            if (evt.type == EVENT_TIME_DATE_CHANGED ||
                evt.type == EVENT_PRAYER_TIMES_UPDATED) {
                ESP_LOGD(TAG, "event %d -- rebuilding state", (int)evt.type);
                reset_state_from_service();
            }
        }

        scheduler_tick();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SCHEDULER_TICK_MS));
    }

    s_task = NULL;
    ESP_LOGI(TAG, "task stopped");
    vTaskDelete(NULL);
}

esp_err_t svc_scheduler_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    memset(&s_state, 0, sizeof(s_state));
    reset_state_from_service();   /* no publish: avoids a boot-time replay */
    s_inited = true;

    ESP_LOGI(TAG, "initialised");
    return ESP_OK;
}

esp_err_t svc_scheduler_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    if (!s_inited) {
        esp_err_t err = svc_scheduler_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    s_mailbox = xQueueCreate(MAILBOX_LEN, sizeof(event_t));
    if (!s_mailbox) {
        ESP_LOGE(TAG, "failed to create mailbox");
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    if (xTaskCreatePinnedToCore(scheduler_task, "scheduler",
                                SCHEDULER_TASK_STACK, NULL, SCHEDULER_TASK_PRIO,
                                &s_task, SCHEDULER_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "failed to create task");
        s_running = false;
        vQueueDelete(s_mailbox);
        s_mailbox = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* A failed subscription is not fatal: the 1 Hz poll alone still fires
       each prayer exactly once (today's epochs are later than yesterday's
       stored ones); it only loses the immediate "re-mark the past" step. */
    if (!event_bus_subscribe(s_mailbox, EVENT_TIME_DATE_CHANGED)) {
        ESP_LOGW(TAG, "subscribe(EVENT_TIME_DATE_CHANGED) failed -- "
                      "midnight/DST rollover relies on the 1 Hz poll");
    }
    if (!event_bus_subscribe(s_mailbox, EVENT_PRAYER_TIMES_UPDATED)) {
        ESP_LOGW(TAG, "subscribe(EVENT_PRAYER_TIMES_UPDATED) failed -- "
                      "config changes rely on the 1 Hz poll");
    }

    s_started = true;
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}
