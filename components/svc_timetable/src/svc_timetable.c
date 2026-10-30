/*
 * svc_timetable.c
 *
 * Year-timetable implementation. See header for API contract.
 *
 * The file is read and parsed with cJSON on svc_timetable_init(). Metadata,
 * today, and tomorrow are extracted, then the cJSON tree is freed. On a
 * date change, set_current_date() re-reads the file (cheap; once per day).
 *
 * Regeneration writes to timetable.json.tmp, fsyncs, then rename() to
 * timetable.json. FATFS supports atomic rename.
 */
#include "svc_timetable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* fsync() */
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "cJSON.h"

#include "svc_prayer_engine.h"
#include "svc_time.h"

static const char *TAG = "svc_timetable";

static bool                    s_initialized = false;
static svc_timetable_metadata_t s_meta;
static svc_timetable_day_t     s_today;
static svc_timetable_day_t     s_tomorrow;
static int                     s_today_ymd    = 0;
static int                     s_tomorrow_ymd = 0;

static void mmdd_from_ymd(int y, int m, int d, char out[6])
{
    snprintf(out, 6, "%02d-%02d", m, d);
    (void)y;
}

static void ymd_advance_one_day(int *y, int *m, int *d)
{
    struct tm t = {0};
    t.tm_year = *y - 1900;
    t.tm_mon  = *m - 1;
    t.tm_mday = *d + 1;
    t.tm_isdst = 0;
    time_t e = timegm(&t);
    struct tm out;
    if (e == (time_t)-1 || gmtime_r(&e, &out) == NULL) return;
    *y = out.tm_year + 1900;
    *m = out.tm_mon + 1;
    *d = out.tm_mday;
}

static bool jget_u32(const cJSON *obj, const char *key, uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) return false;
    double v = item->valuedouble;
    if (v < 0.0 || v > 200000.0) return false;
    *out = (uint32_t)(v + 0.5);
    return true;
}

static void extract_day_entry(const cJSON *obj, svc_timetable_day_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(obj)) return;

    out->has_fajr     = jget_u32(obj, "fajr",     &out->fajr);
    out->has_sunrise  = jget_u32(obj, "sunrise",  &out->sunrise);
    out->has_dhuhr    = jget_u32(obj, "dhuhr",    &out->dhuhr);
    out->has_asr      = jget_u32(obj, "asr",      &out->asr);
    out->has_maghrib  = jget_u32(obj, "maghrib",  &out->maghrib);
    out->has_isha     = jget_u32(obj, "isha",     &out->isha);
    out->has_midnight    = jget_u32(obj, "midnight",    &out->midnight);
    out->has_zawal_start = jget_u32(obj, "zawal_start", &out->zawal_start);
    out->has_asr_yellow  = jget_u32(obj, "asr_yellow",  &out->asr_yellow);
    out->has_sehri_end   = jget_u32(obj, "sehri_end",   &out->sehri_end);
    out->has_iftar       = jget_u32(obj, "iftar",       &out->iftar);

    const cJSON *jam = cJSON_GetObjectItemCaseSensitive(obj, "jamat");
    if (cJSON_IsArray(jam)) {
        int n = cJSON_GetArraySize(jam);
        for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT && i < n; i++) {
            const cJSON *el = cJSON_GetArrayItem(jam, i);
            if (cJSON_IsNumber(el)) {
                double v = el->valuedouble;
                if (v >= 0.0 && v <= 200000.0) {
                    out->jamat[i] = (uint32_t)(v + 0.5);
                    out->has_jamat[i] = true;
                }
            }
        }
    }
}

/* Parse the file into metadata + today + tomorrow without building the
   whole cJSON tree. Reads the file into a fixed 128 KB heap buffer, then
   walks the `days` array, parsing only the entries matching today's and
   tomorrow's MM-DD. Peak heap: ~128 KB for the file buffer + ~1 KB per
   parsed entry. */
static esp_err_t parse_file(void)
{
    FILE *f = fopen(SVC_TIMETABLE_PATH, "r");
    if (!f) return ESP_ERR_NOT_FOUND;

    /* Read into a fixed 128KB buffer to avoid FATFS fseek/ftell bugs.
       (A full year JSON with 14 keys + jamat is ~83KB). */
    char *buf = malloc(131072);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    size_t rd = fread(buf, 1, 131071, f);
    fclose(f);
    if (rd == 0) { free(buf); return ESP_FAIL; }
    buf[rd] = '\0';

    memset(&s_meta, 0, sizeof(s_meta));
    memset(&s_today, 0, sizeof(s_today));
    memset(&s_tomorrow, 0, sizeof(s_tomorrow));
    s_today_ymd = 0;
    s_tomorrow_ymd = 0;
    s_meta.file_present = true;

    /* --- Parse header (small, use cJSON on just the header object) --- */
    /* The header is the top-level object *except* the "days" array. We
       use cJSON_ParseWithLength for just the part before "days":[...]. */
    const char *days_marker = strstr(buf, "\"days\"");
    if (!days_marker) { free(buf); return ESP_ERR_INVALID_RESPONSE; }
    const char *bracket = strchr(days_marker, '[');
    if (!bracket) { free(buf); return ESP_ERR_INVALID_RESPONSE; }

    /* Header length: everything up to the comma before "days" */
    size_t hdr_len = (size_t)(days_marker - buf);
    /* Trim trailing whitespace and comma */
    while (hdr_len > 0 && (buf[hdr_len-1] == ',' || buf[hdr_len-1] == ' ' || buf[hdr_len-1] == '\n' || buf[hdr_len-1] == '\r' || buf[hdr_len-1] == '\t')) {
        hdr_len--;
    }
    /* Append closing brace to form a valid JSON object */
    char *hdr = malloc(hdr_len + 2);
    if (!hdr) { free(buf); return ESP_ERR_NO_MEM; }
    memcpy(hdr, buf, hdr_len);
    hdr[hdr_len] = '}';
    hdr[hdr_len + 1] = '\0';

    cJSON *root = cJSON_Parse(hdr);
    free(hdr);
    if (!root) { free(buf); return ESP_ERR_INVALID_RESPONSE; }

    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    if (!cJSON_IsNumber(ver) || ver->valueint > SVC_TIMETABLE_SCHEMA_VER) {
        ESP_LOGW(TAG, "schema_version unsupported (%d > %d)",
                 cJSON_IsNumber(ver) ? ver->valueint : -1, SVC_TIMETABLE_SCHEMA_VER);
        cJSON_Delete(root);
        free(buf);
        return ESP_ERR_INVALID_VERSION;
    }

    const cJSON *src = cJSON_GetObjectItemCaseSensitive(root, "source");
    if (cJSON_IsString(src) && strcmp(src->valuestring, "firmware") == 0) {
        s_meta.source = SVC_TIMETABLE_SOURCE_FIRMWARE;
    } else {
        s_meta.source = SVC_TIMETABLE_SOURCE_USER;
    }

    const cJSON *yr = cJSON_GetObjectItemCaseSensitive(root, "year");
    if (cJSON_IsNumber(yr)) {
        s_meta.year = yr->valueint;
    } else {
        s_meta.year = SVC_TIMETABLE_YEAR_PERMANENT;
    }

    const cJSON *gen = cJSON_GetObjectItemCaseSensitive(root, "generated_at");
    if (cJSON_IsString(gen)) {
        strncpy(s_meta.generated_at, gen->valuestring, sizeof(s_meta.generated_at)-1);
    }

    const cJSON *ji = cJSON_GetObjectItemCaseSensitive(root, "jamat_included");
    s_meta.jamat_included = cJSON_IsTrue(ji);

    const cJSON *calc = cJSON_GetObjectItemCaseSensitive(root, "calculation");
    if (cJSON_IsObject(calc)) {
        char *s = cJSON_PrintUnformatted(calc);
        if (s) {
            strncpy(s_meta.calculation_json, s, sizeof(s_meta.calculation_json)-1);
            cJSON_free(s);
        }
    }
    cJSON_Delete(root);

    /* --- Walk the days array, parsing only today + tomorrow --- */
    svc_time_snapshot_t snap;
    if (svc_time_get_snapshot(&snap) != ESP_OK || !snap.valid) {
        free(buf);
        return ESP_OK;   /* metadata populated, no today/tomorrow */
    }
    int y = snap.local.year, m = snap.local.month, d = snap.local.day;
    s_today_ymd = y * 10000 + m * 100 + d;
    char today_mmdd[6];
    mmdd_from_ymd(y, m, d, today_mmdd);

    int ty = y, tm = m, td = d;
    ymd_advance_one_day(&ty, &tm, &td);
    s_tomorrow_ymd = ty * 10000 + tm * 100 + td;
    char tomorrow_mmdd[6];
    mmdd_from_ymd(ty, tm, td, tomorrow_mmdd);

    bool today_found = false, tomorrow_found = false;
    bool any_incomplete = false;
    uint16_t day_count = 0;

    const char *p = bracket + 1;
    while (*p) {
        if (*p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p != '{') { p++; continue; }

        const char *entry_start = p;
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            else if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\' && *(p+1)) p++;
                    p++;
                }
                if (*p) p++;
                continue;
            }
            p++;
        }
        if (depth != 0) break;   /* malformed */
        size_t entry_len = (size_t)(p - entry_start);
        day_count++;

        /* Find "d":"MM-DD" within this entry */
        const char *d_marker = strstr(entry_start, "\"d\":\"");
        if (!d_marker || d_marker >= p) continue;
        d_marker += 5;
        if ((size_t)(p - d_marker) < 6) continue;

        bool is_today    = (strncmp(d_marker, today_mmdd, 5) == 0);
        bool is_tomorrow = (strncmp(d_marker, tomorrow_mmdd, 5) == 0);

        if (!is_today && !is_tomorrow) {
            /* Still need to know if ANY entry is partial (for the UI flag) */
            if (!any_incomplete && !strstr(entry_start, "\"jamat\"")) {
                any_incomplete = true;
            }
            continue;
        }

        /* Parse just this entry */
        char *entry_copy = malloc(entry_len + 1);
        if (!entry_copy) continue;
        memcpy(entry_copy, entry_start, entry_len);
        entry_copy[entry_len] = '\0';

        cJSON *j_entry = cJSON_Parse(entry_copy);
        free(entry_copy);
        if (!j_entry) continue;

        if (is_today) {
            extract_day_entry(j_entry, &s_today);
            today_found = true;
        } else {
            extract_day_entry(j_entry, &s_tomorrow);
            tomorrow_found = true;
        }
        cJSON_Delete(j_entry);
    }

    s_meta.day_count = day_count;
    s_meta.partial = any_incomplete || (!today_found) || (!tomorrow_found);

    free(buf);
    ESP_LOGD(TAG, "parsed: days=%u today=%d tomorrow=%d partial=%d",
             (unsigned)day_count, (int)today_found, (int)tomorrow_found,
             (int)s_meta.partial);
    return ESP_OK;
}

static void serialize_calc(const svc_config_t *cfg, const char *tz,
                           char out[1024])
{
    int n = snprintf(out, 1024,
        "{\"method\":%d,\"madhab\":%d,\"high_lat_rule\":%d,"
        "\"custom_fajr_angle\":%.3f,\"custom_isha_angle\":%.3f,"
        "\"latitude\":%.6f,\"longitude\":%.6f,\"timezone\":\"%s\","
        "\"zawal_buffer_min\":%u,"
        "\"season\":{\"sum_m\":%u,\"sum_d\":%u,\"win_m\":%u,\"win_d\":%u},",
        (int)cfg->method, (int)cfg->madhab, (int)cfg->high_lat_rule,
        cfg->custom_fajr_angle, cfg->custom_isha_angle,
        cfg->latitude, cfg->longitude, tz,
        (unsigned)cfg->zawal_buffer_min,
        (unsigned)cfg->season.summer_start_month, (unsigned)cfg->season.summer_start_day,
        (unsigned)cfg->season.winter_start_month, (unsigned)cfg->season.winter_start_day);
    n += snprintf(out+n, 1024-n, "\"adhan_offsets\":[");
    for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT; i++) {
        n += snprintf(out+n, 1024-n, "%s%u", i?",":"",
                      (unsigned)cfg->adhan_offset_min[i]);
    }
    n += snprintf(out+n, 1024-n, "],\"jamat\":[");
    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        n += snprintf(out+n, 1024-n,
            "%s{\"wa\":%u,\"wo\":%u,\"sa\":%u,\"so\":%u,\"r\":%u}",
            p?",":"",
            (unsigned)cfg->jamat[p].winter.anchor,
            (unsigned)cfg->jamat[p].winter.offset_min,
            (unsigned)cfg->jamat[p].summer.anchor,
            (unsigned)cfg->jamat[p].summer.offset_min,
            (unsigned)cfg->jamat[p].round_min);
    }
    snprintf(out+n, 1024-n, "]}");
}

esp_err_t svc_timetable_init(void)
{
    memset(&s_meta, 0, sizeof(s_meta));
    memset(&s_today, 0, sizeof(s_today));
    memset(&s_tomorrow, 0, sizeof(s_tomorrow));
    s_meta.source = SVC_TIMETABLE_SOURCE_NONE;

    esp_err_t err = parse_file();
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no usable timetable: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "timetable loaded: source=%s year=%ld days=%u jamat=%d partial=%d "
                      "heap=%u",
                 s_meta.source == SVC_TIMETABLE_SOURCE_FIRMWARE ? "firmware" : "user",
                 (long)s_meta.year, (unsigned)s_meta.day_count,
                 (int)s_meta.jamat_included, (int)s_meta.partial,
                 (unsigned)esp_get_free_heap_size());
    }
    s_initialized = true;
    return err;
}

esp_err_t svc_timetable_reload(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return svc_timetable_init();
}

esp_err_t svc_timetable_get_metadata(svc_timetable_metadata_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    *out = s_meta;
    return ESP_OK;
}

esp_err_t svc_timetable_get_today(svc_timetable_day_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    *out = s_today;
    return ESP_OK;
}

esp_err_t svc_timetable_get_tomorrow(svc_timetable_day_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    *out = s_tomorrow;
    return ESP_OK;
}

esp_err_t svc_timetable_set_current_date(int year, int month, int day)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    int ymd = year * 10000 + month * 100 + day;
    if (ymd == s_today_ymd) return ESP_OK;
    return parse_file();
}

bool svc_timetable_needs_regen_from_config(void)
{
    if (!s_initialized || !s_meta.file_present) return true;
    if (s_meta.source != SVC_TIMETABLE_SOURCE_FIRMWARE) return false;

    /* Year rollover: last year's file must never serve this year's dates.
       (mm-dd keys would still match, so a stale year is a silent wrong-time
       bug -- regenerate instead.) */
    svc_time_snapshot_t snap;
    if (svc_time_get_snapshot(&snap) == ESP_OK && snap.valid
        && s_meta.year != SVC_TIMETABLE_YEAR_PERMANENT
        && (int32_t)snap.local.year != s_meta.year) {
        ESP_LOGI(TAG, "timetable year %ld != current %d -- regenerating",
                 (long)s_meta.year, (int)snap.local.year);
        return true;
    }

    svc_config_t cfg;
    if (svc_config_get(&cfg) != ESP_OK) return false;

    const char *tz = svc_time_get_timezone_posix();
    char *current = malloc(1024);
    if (!current) return false;

    serialize_calc(&cfg, tz, current);
    bool needs = (strcmp(current, s_meta.calculation_json) != 0);
    free(current);
    return needs;
}

static cJSON *build_day_entry(const svc_config_t *cfg,
                              int year, int month, int day,
                              int tz_offset_min)
{
    double tz_off = (double)tz_offset_min / 60.0;
    svc_prayer_times_t raw = {0};
    esp_err_t err = svc_prayer_engine_calculate(
        year, month, day,
        cfg->latitude, cfg->longitude, tz_off,
        (svc_prayer_method_t)cfg->method,
        (svc_prayer_madhab_t)cfg->madhab,
        (svc_prayer_highlat_t)cfg->high_lat_rule,
        cfg->custom_fajr_angle, cfg->custom_isha_angle, &raw);
    if (err != ESP_OK) return NULL;

    /* Adhan tier: config prayer order, each with its adhan_offset_min.
       Sunrise is written separately -- it is not a prayer and carries no
       offset (same contract as svc_prayer_engine_service). */
    uint32_t adhan[SVC_CONFIG_PRAYER_COUNT];
    adhan[SVC_CONFIG_PRAYER_FAJR]    = raw.fajr_sec
        + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_FAJR]    * 60u;
    adhan[SVC_CONFIG_PRAYER_DHUHR]   = raw.dhuhr_sec
        + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_DHUHR]   * 60u;
    adhan[SVC_CONFIG_PRAYER_ASR]     = raw.asr_sec
        + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_ASR]     * 60u;
    adhan[SVC_CONFIG_PRAYER_MAGHRIB] = raw.maghrib_sec
        + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_MAGHRIB] * 60u;
    adhan[SVC_CONFIG_PRAYER_ISHA]    = raw.isha_sec
        + (uint32_t)cfg->adhan_offset_min[SVC_CONFIG_PRAYER_ISHA]    * 60u;

    uint8_t season = 0;
    {
        uint16_t v = (uint16_t)month * 100u + (uint16_t)day;
        uint16_t s = (uint16_t)cfg->season.summer_start_month * 100u
                   + (uint16_t)cfg->season.summer_start_day;
        uint16_t w = (uint16_t)cfg->season.winter_start_month * 100u
                   + (uint16_t)cfg->season.winter_start_day;
        bool in_summer = (s <= w) ? (v >= s && v < w) : (v >= s || v < w);
        season = in_summer ? 1 : 0;
    }

    uint32_t jamat[SVC_CONFIG_PRAYER_COUNT];
    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        const jamat_season_cfg_t *jc = (season == 0)
            ? &cfg->jamat[p].winter : &cfg->jamat[p].summer;
        uint32_t off_s = (uint32_t)jc->offset_min * 60u;
        uint32_t base = 0;
        bool valid = true;
        switch (jc->anchor) {
        case SVC_CONFIG_JAMAT_ADHAN_PLUS:
            base = adhan[p] + off_s;
            break;
        case SVC_CONFIG_JAMAT_SUNRISE_MINUS:
            if (raw.sunrise_sec == UINT32_MAX || raw.sunrise_sec < off_s) { valid = false; }
            else { base = raw.sunrise_sec - off_s; }
            break;
        case SVC_CONFIG_JAMAT_MIDNIGHT_MINUS:
            if (raw.midnight_sec == UINT32_MAX || raw.midnight_sec < off_s) { valid = false; }
            else { base = raw.midnight_sec - off_s; }
            break;
        default:
            valid = false;
            break;
        }
        if (valid) {
            uint8_t rm = cfg->jamat[p].round_min;
            if (rm == 0) {
                jamat[p] = base;
            } else {
                uint32_t step = (uint32_t)rm * 60u;
                uint32_t half = step / 2u;
                jamat[p] = ((base + half) / step) * step;
            }
        } else {
            jamat[p] = UINT32_MAX;
        }
    }

    cJSON *o = cJSON_CreateObject();
    char mmdd[6];
    mmdd_from_ymd(year, month, day, mmdd);
    cJSON_AddStringToObject(o, "d", mmdd);
    cJSON_AddNumberToObject(o, "fajr",     adhan[SVC_CONFIG_PRAYER_FAJR]);
    cJSON_AddNumberToObject(o, "sunrise",  raw.sunrise_sec);
    cJSON_AddNumberToObject(o, "dhuhr",    adhan[SVC_CONFIG_PRAYER_DHUHR]);
    cJSON_AddNumberToObject(o, "asr",      adhan[SVC_CONFIG_PRAYER_ASR]);
    cJSON_AddNumberToObject(o, "maghrib",  adhan[SVC_CONFIG_PRAYER_MAGHRIB]);
    cJSON_AddNumberToObject(o, "isha",     adhan[SVC_CONFIG_PRAYER_ISHA]);
    cJSON_AddNumberToObject(o, "midnight",     raw.midnight_sec);
    uint32_t zw = (uint32_t)cfg->zawal_buffer_min * 60u;
    cJSON_AddNumberToObject(o, "zawal_start",  raw.dhuhr_sec > zw ? raw.dhuhr_sec - zw : 0u);
    if (raw.asr_yellow_sec == UINT32_MAX) {
        cJSON_AddNullToObject(o, "asr_yellow");
    } else {
        cJSON_AddNumberToObject(o, "asr_yellow", raw.asr_yellow_sec);
    }
    cJSON_AddNumberToObject(o, "sehri_end", raw.fajr_sec);
    cJSON_AddNumberToObject(o, "iftar",     raw.sunset_sec);

    cJSON *ja = cJSON_AddArrayToObject(o, "jamat");
    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        if (jamat[p] == UINT32_MAX) {
            cJSON_AddItemToArray(ja, cJSON_CreateNull());
        } else {
            cJSON_AddItemToArray(ja, cJSON_CreateNumber(jamat[p]));
        }
    }
    return o;
}

esp_err_t svc_timetable_generate_full_year(void)
{
    svc_config_t cfg;
    esp_err_t err = svc_config_get(&cfg);
    if (err != ESP_OK) return err;
    if (!cfg.location_valid) return ESP_ERR_INVALID_STATE;

    svc_time_snapshot_t snap;
    err = svc_time_get_snapshot(&snap);
    if (err != ESP_OK || !snap.valid) return ESP_ERR_INVALID_STATE;

    int year = snap.local.year;
    if (year < 2020 || year > 2100) {
        ESP_LOGW(TAG, "year %d outside [2020,2100] -- refusing to generate", year);
        return ESP_ERR_INVALID_STATE;
    }

    const char *tz = svc_time_get_timezone_posix();

    FILE *f = fopen(SVC_TIMETABLE_PATH_TMP, "w");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s for write", SVC_TIMETABLE_PATH_TMP);
        return ESP_FAIL;
    }

    /* Stream the header (no cJSON tree). */
    char calc_json[1024];
    serialize_calc(&cfg, tz, calc_json);

    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    char gen[32];
    strftime(gen, sizeof(gen), "%Y-%m-%dT%H:%M:%SZ", &tmv);

    char header[1280];
    int hdr_len = snprintf(header, sizeof(header),
        "{\"schema_version\":%d,\"source\":\"firmware\",\"year\":%d,"
        "\"generated_at\":\"%s\",\"calculation\":%s,"
        "\"jamat_included\":true,\"days\":[",
        SVC_TIMETABLE_SCHEMA_VER, year, gen, calc_json);
    if (hdr_len <= 0 || (size_t)hdr_len >= sizeof(header)) {
        fclose(f); remove(SVC_TIMETABLE_PATH_TMP);
        return ESP_FAIL;
    }
    if (fwrite(header, 1, (size_t)hdr_len, f) != (size_t)hdr_len) {
        fclose(f); remove(SVC_TIMETABLE_PATH_TMP);
        return ESP_FAIL;
    }

    int days_total = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 366 : 365;
    ESP_LOGI(TAG, "streaming %d days for year %d (heap free before: %u)",
             days_total, year, (unsigned)esp_get_free_heap_size());

    bool wrote_any = false;
    for (int i = 1; i <= days_total; i++) {
        struct tm t = {0};
        t.tm_year = year - 1900;
        t.tm_mon  = 0;
        t.tm_mday = i;
        t.tm_isdst = 0;
        time_t e = timegm(&t);
        struct tm out;
        if (e == (time_t)-1 || gmtime_r(&e, &out) == NULL) continue;

        struct tm local_noon = out;
        local_noon.tm_hour = 12;
        local_noon.tm_isdst = -1;
        time_t el = mktime(&local_noon);
        struct tm utc_noon = out;
        utc_noon.tm_hour = 12;
        utc_noon.tm_isdst = 0;
        time_t eu = timegm(&utc_noon);
        int tz_min = (el != (time_t)-1 && eu != (time_t)-1)
                     ? (int)((eu - el) / 60) : 0;

        cJSON *entry = build_day_entry(&cfg,
            out.tm_year + 1900, out.tm_mon + 1, out.tm_mday, tz_min);
        if (!entry) continue;

        char *entry_str = cJSON_PrintUnformatted(entry);
        cJSON_Delete(entry);   /* free the tree immediately */
        if (!entry_str) continue;

        if (wrote_any) fputc(',', f);
        fwrite(entry_str, 1, strlen(entry_str), f);
        cJSON_free(entry_str);
        wrote_any = true;
    }

    fputs("]}", f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (rename(SVC_TIMETABLE_PATH_TMP, SVC_TIMETABLE_PATH) != 0) {
        ESP_LOGW(TAG, "rename failed, removing destination and retrying");
        remove(SVC_TIMETABLE_PATH);
        if (rename(SVC_TIMETABLE_PATH_TMP, SVC_TIMETABLE_PATH) != 0) {
            ESP_LOGE(TAG, "rename failed again!");
            remove(SVC_TIMETABLE_PATH_TMP);
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "wrote %d days (heap free after: %u)",
             days_total, (unsigned)esp_get_free_heap_size());

    return svc_timetable_reload();
}

esp_err_t svc_timetable_delete(void)
{
    if (remove(SVC_TIMETABLE_PATH) != 0 && errno != ENOENT) {
        return ESP_FAIL;
    }
    memset(&s_meta, 0, sizeof(s_meta));
    memset(&s_today, 0, sizeof(s_today));
    memset(&s_tomorrow, 0, sizeof(s_tomorrow));
    s_today_ymd    = 0;
    s_tomorrow_ymd = 0;
    s_meta.source = SVC_TIMETABLE_SOURCE_NONE;
    return ESP_OK;
}

svc_timetable_source_t svc_timetable_get_source(void)
{
    return s_meta.file_present ? s_meta.source : SVC_TIMETABLE_SOURCE_NONE;
}
