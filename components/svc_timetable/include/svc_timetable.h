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
/* Separate temp files: an in-flight user upload must not be overwritten by
   the background regenerator, and vice versa. Both writers produce a
   complete file then rename() to SVC_TIMETABLE_PATH; the rename is atomic
   at FATFS level, so the live file is never seen half-written. */
#define SVC_TIMETABLE_UPLOAD_TMP  "/storage/timetable_upload.tmp"
#define SVC_TIMETABLE_REGEN_TMP   "/storage/timetable_regen.tmp"
/* Legacy alias: SVC_TIMETABLE_PATH_TMP was the single shared tmp path before
   PR 4. It now resolves to the regenerator's temp file so the body of
   svc_timetable_generate_full_year() does not need editing. Do NOT use it in
   new code; pick UPLOAD_TMP or REGEN_TMP explicitly. */
#define SVC_TIMETABLE_PATH_TMP    SVC_TIMETABLE_REGEN_TMP
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

/*
 * Acquire / release the writer lock. Used by the HTTP upload handler, which
 * performs its own file writes (streaming from the HTTP body) but must not
 * let a background regeneration run between its write and its rename: without
 * this, an upload that lands mid-regen can be silently overwritten when the
 * regenerator finally renames its own temp file into place.
 *
 * Paired: every successful acquire must be matched by one release. Do not
 * call any other svc_timetable_* function between acquire and release except
 * svc_timetable_reload() and svc_timetable_validate() -- those are safe and
 * were designed for this use.
 *
 * Returns ESP_OK on success, ESP_ERR_TIMEOUT if the writer lock cannot be
 * taken within 1 s (caller should reply 503 Service Unavailable to the
 * client), ESP_ERR_INVALID_STATE if the service has not been initialised.
 */
esp_err_t svc_timetable_acquire_writer(void);
void      svc_timetable_release_writer(void);

/*
 * Parse `path` into a private temporary and validate it. Does NOT touch the
 * live timetable state or SVC_TIMETABLE_PATH. Returns:
 *   ESP_OK                    -- supported, non-empty timetable
 *   ESP_ERR_INVALID_ARG       -- path is NULL
 *   ESP_ERR_NOT_FOUND         -- path does not exist / cannot be opened
 *   ESP_ERR_INVALID_VERSION   -- schema_version is unsupported
 *   ESP_ERR_INVALID_RESPONSE  -- JSON malformed, or day_count == 0
 *   ESP_ERR_NO_MEM / ESP_FAIL -- read or allocation failure
 *
 * Called by the HTTP upload handler after the writer lock is held, before
 * the atomic rename. On any non-OK return the caller removes the candidate
 * file and leaves SVC_TIMETABLE_PATH unchanged.
 */
esp_err_t svc_timetable_validate(const char *path);

#ifdef __cplusplus
}
#endif
