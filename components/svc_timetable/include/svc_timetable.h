/*
 * svc_timetable.h
 *
 * Year-calculated prayer timetable. Caches today's and tomorrow's entry
 * in RAM; the file itself lives on /storage/timetable.json (FATFS).
 *
 * Both firmware-generated and user-uploaded files store FINAL local-time
 * seconds. No astronomical values, no offsets.
 *
 * Firmware-generated files fill every field. User-uploaded files may omit
 * any field per day; missing fields are computed from current config on
 * read by the caller.
 *
 * See docs/prayer-times-model.md §5.5 for the full schema.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "svc_config.h"   /* SVC_CONFIG_PRAYER_COUNT */

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_TIMETABLE_PATH        "/storage/timetable.json"
#define SVC_TIMETABLE_PATH_TMP    "/storage/timetable.json.tmp"
#define SVC_TIMETABLE_SCHEMA_VER  1
#define SVC_TIMETABLE_YEAR_PERMANENT (-1)

typedef enum {
    SVC_TIMETABLE_SOURCE_NONE     = 0,
    SVC_TIMETABLE_SOURCE_FIRMWARE = 1,
    SVC_TIMETABLE_SOURCE_USER     = 2,
} svc_timetable_source_t;

typedef struct {
    bool has_fajr, has_sunrise, has_dhuhr, has_asr, has_maghrib, has_isha;
    bool has_midnight, has_zawal_start, has_asr_yellow;
    bool has_sehri_end, has_iftar;
    bool has_jamat[SVC_CONFIG_PRAYER_COUNT];

    uint32_t fajr, sunrise, dhuhr, asr, maghrib, isha;
    uint32_t midnight, zawal_start, asr_yellow, sehri_end, iftar;
    uint32_t jamat[SVC_CONFIG_PRAYER_COUNT];
} svc_timetable_day_t;

typedef struct {
    svc_timetable_source_t source;
    int32_t                year;          /* SVC_TIMETABLE_YEAR_PERMANENT if file's year was null */
    uint16_t               day_count;
    char                   generated_at[32];
    bool                   jamat_included;
    bool                   file_present;
    bool                   partial;       /* true if any day has any field missing */
    char                   calculation_json[1024];
} svc_timetable_metadata_t;

esp_err_t svc_timetable_init(void);
esp_err_t svc_timetable_reload(void);
esp_err_t svc_timetable_get_metadata(svc_timetable_metadata_t *out);
svc_timetable_source_t svc_timetable_get_source(void);
esp_err_t svc_timetable_get_today(svc_timetable_day_t *out);
esp_err_t svc_timetable_get_tomorrow(svc_timetable_day_t *out);
esp_err_t svc_timetable_set_current_date(int year, int month, int day);
esp_err_t svc_timetable_generate_full_year(void);
esp_err_t svc_timetable_delete(void);
bool      svc_timetable_needs_regen_from_config(void);

#ifdef __cplusplus
}
#endif
