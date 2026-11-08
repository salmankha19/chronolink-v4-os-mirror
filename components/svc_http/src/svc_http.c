#include "svc_http.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "svc_config.h"
#include "svc_network.h"
#include "svc_prayer_engine_service.h"
#include "svc_timetable.h"
#include "svc_time.h"   /* /debug/ntp hooks + preset TZ application (R4 #132) */
#include "esp_system.h"
#include "esp_random.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

/* Compile-time gate: POST /api/ota must not ship without rollback
 * enabled. CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y (PR 4.6) makes a
 * failed OTA image revert automatically. Without it, a bad image that
 * crashes before app_main marks it valid would brick the device. */
#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "POST /api/ota requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y."
#endif

/* OTA shared secret. Required in the X-OTA-Token header on POST /api/ota.
 * Compile-time; changing it requires a rebuild. Prevents drive-by flashing
 * from the open AP setup network (ChronoLink-Setup). Override via
 * -DOTA_SHARED_TOKEN=... in build_flags or edit the default below. */
#ifndef OTA_SHARED_TOKEN
#define OTA_SHARED_TOKEN "change-me-before-flashing"
#endif

#define OTA_CHUNK_SIZE 4096

/* Streaming read timeouts. Each `httpd_req_recv` blocks for the server's
   configured recv timeout (default 5 s) before returning
   HTTPD_SOCK_ERR_TIMEOUT. Fail after N consecutive timeouts. Chosen so a
   transient 5 s stall is tolerated but a dead client is not. */
#define UPLOAD_MAX_CONSECUTIVE_TIMEOUTS  2   /* ~10 s at 5 s per recv */
#define OTA_MAX_CONSECUTIVE_TIMEOUTS     6   /* ~30 s at 5 s per recv */

extern const unsigned char index_html[];
extern const size_t index_html_len;
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>   /* fsync(), fileno() for the timetable upload */

static const char *TAG = "svc_http";
static httpd_handle_t s_server = NULL;

/* -------------------------------------------------------------------------
 * CSRF defence for state-changing endpoints.
 *
 * A browser page on the same WiFi can issue cross-origin POSTs to the
 * device: the same-origin policy does not block <form> submissions or
 * simple fetch() calls without a custom header. Requiring a custom header
 * forces a CORS preflight for any cross-origin request; the device never
 * approves a preflight, so the browser drops the request before it reaches
 * us. Requests that carry the header must also prove knowledge of the
 * device token, which is stored in NVS, printed once to serial at first
 * boot, and pasted into the web UI by the installer.
 *
 * The token protects against CSRF, not against an attacker with physical
 * access to the device or serial console. That threat model is out of
 * scope.
 * ------------------------------------------------------------------------- */

#define NVS_NAMESPACE_SVC_HTTP  "svc_http"
#define NVS_KEY_API_TOKEN       "api_token"
#define API_TOKEN_HEX_LEN       64
#define AUTH_HEADER             "X-Chronolink-Auth"
#define AUTH_SCHEME             "Bearer "

static char s_api_token[API_TOKEN_HEX_LEN + 1];

static esp_err_t load_or_generate_api_token(void)
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE_SVC_HTTP, NVS_READWRITE, &h);
    bool nvs_open_failed = (err != ESP_OK);
    if (nvs_open_failed) {
        ESP_LOGE(TAG, "nvs_open('%s') failed: %s -- generating ephemeral token",
                 NVS_NAMESPACE_SVC_HTTP, esp_err_to_name(err));
    } else {
        size_t len = sizeof(s_api_token);
        esp_err_t rd_err = nvs_get_str(h, NVS_KEY_API_TOKEN, s_api_token, &len);
        if (rd_err == ESP_OK && len == API_TOKEN_HEX_LEN + 1) {
            nvs_close(h);
            ESP_LOGI(TAG, "API token loaded from NVS");
            return ESP_OK;
        }
    }

    uint8_t raw[32];
    esp_fill_random(raw, sizeof(raw));
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(raw); i++) {
        s_api_token[2 * i]     = hex[raw[i] >> 4];
        s_api_token[2 * i + 1] = hex[raw[i] & 0x0F];
    }
    s_api_token[API_TOKEN_HEX_LEN] = '\0';

    bool persisted = false;
    if (!nvs_open_failed) {
        esp_err_t w_err = nvs_set_str(h, NVS_KEY_API_TOKEN, s_api_token);
        if (w_err == ESP_OK) w_err = nvs_commit(h);
        nvs_close(h);
        if (w_err != ESP_OK) {
            ESP_LOGE(TAG, "failed to persist API token: %s",
                     esp_err_to_name(w_err));
        } else {
            persisted = true;
        }
    }

    ESP_LOGW(TAG, "=====================================================");
    ESP_LOGW(TAG, " Generated API token (store this, it is shown once):");
    ESP_LOGW(TAG, "   %s", s_api_token);
    ESP_LOGW(TAG, " Paste it into the Setup Authentication field.");
    if (!persisted) {
        ESP_LOGW(TAG, " *** TOKEN IS EPHEMERAL -- will change on reboot ***");
        ESP_LOGW(TAG, " *** NVS unavailable: config changes cannot be saved  ***");
    }
    ESP_LOGW(TAG, "=====================================================");
    return ESP_OK;
}

/* Constant-time compare. Avoids early exit on the first differing byte. */
static bool ct_str_eq(const char *a, const char *b, size_t len)
{
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    }
    return diff == 0;
}

/* Returns true on success. On failure sends a 403 JSON response and returns
   false; the caller must `return ESP_FAIL;` immediately. */
static bool require_auth(httpd_req_t *req)
{
    char hdr[128];
    if (httpd_req_get_hdr_value_str(req, AUTH_HEADER, hdr, sizeof(hdr)) != ESP_OK) {
        ESP_LOGW(TAG, "%s: missing %s header", req->uri, AUTH_HEADER);
        goto deny;
    }

    const size_t scheme_len = sizeof(AUTH_SCHEME) - 1;
    if (strlen(hdr) != scheme_len + API_TOKEN_HEX_LEN ||
        strncmp(hdr, AUTH_SCHEME, scheme_len) != 0) {
        ESP_LOGW(TAG, "%s: malformed %s header", req->uri, AUTH_HEADER);
        goto deny;
    }

    if (!ct_str_eq(hdr + scheme_len, s_api_token, API_TOKEN_HEX_LEN)) {
        ESP_LOGW(TAG, "%s: wrong token", req->uri);
        goto deny;
    }

    return true;

deny:
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"forbidden\"}");
    return false;
}

/* One-shot boot request written by POST /api/factory-reset and read by
 * boot_manager_init() on the next boot. MUST stay in sync with
 * components/boot_manager/src/boot_manager.c. */
#define NVS_NAMESPACE_BOOT_REQUEST      "boot_req"
#define NVS_KEY_BOOT_REQUEST            "flag"
#define NVS_BOOT_REQUEST_FACTORY_RESET  0x02u  /* == BOOT_FLAG_FACTORY_RESET */

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    return err;
}

/* Read up to `max_len` bytes from the request body into a NUL-terminated heap buffer. */
static char *read_body(httpd_req_t *req, size_t max_len, int *out_len)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= (int)max_len) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid or too-large payload");
        return NULL;
    }
    char *buf = malloc((size_t)total_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return NULL;
    }
    int cur_len = 0;
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, (size_t)(total_len - cur_len));
        if (received <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Recv failed");
            return NULL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    if (out_len) *out_len = total_len;
    return buf;
}

/* Returns true and writes *out when `item` is a JSON number whose value is
   integral (no fractional part) and within [min, max]. Returns false for
   non-number items, non-integral values (1.5), NaN, or out-of-range values.
   The caller distinguishes "field absent" from "field invalid" by checking
   the item pointer itself before calling this.

   Why: casting `item->valueint` (or `(uint8_t)item->valueint`) before any
   range check lets `{"high_lat_rule": 257}` silently become 1 via uint8_t
   truncation, and lets `{"jamat": [{"round_min": 258}]}` pass a "must be in
   {0,2,5,10,15,20,30}" check. Same for `zawal_buffer_min` and jamat
   offsets: 65546 wraps to 10, 65536+N wraps to N. This helper is the single
   place that validates integral + range before any int cast. */
static bool json_get_uint_range(const cJSON *item, double min, double max, double *out)
{
    if (!cJSON_IsNumber(item)) return false;
    double v = item->valuedouble;
    if (!(v >= min && v <= max)) return false;   /* NaN-safe */
    if (v != (double)(int)v) return false;       /* reject 1.5, 1.9, ... */
    *out = v;
    return true;
}

/* GET / (Serve HTML) */
static esp_err_t get_html_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html, index_html_len);
}

/* GET /api/config */
static esp_err_t get_config_handler(httpd_req_t *req)
{
    svc_config_t cfg;
    if (svc_config_get(&cfg) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "location_name", cfg.location_name);
    cJSON_AddBoolToObject(root, "location_valid", cfg.location_valid);
    cJSON_AddNumberToObject(root, "latitude", cfg.latitude);
    cJSON_AddNumberToObject(root, "longitude", cfg.longitude);
    cJSON_AddNumberToObject(root, "method", cfg.method);
    cJSON_AddNumberToObject(root, "madhab", cfg.madhab);
    cJSON_AddNumberToObject(root, "custom_fajr_angle", cfg.custom_fajr_angle);
    cJSON_AddNumberToObject(root, "custom_isha_angle", cfg.custom_isha_angle);
    cJSON_AddNumberToObject(root, "adhan_mode", cfg.adhan_mode);
    cJSON_AddNumberToObject(root, "adhan_volume", cfg.adhan_volume);
    cJSON_AddNumberToObject(root, "brightness_mode", cfg.brightness_mode);
    cJSON_AddNumberToObject(root, "high_lat_rule", cfg.high_lat_rule);
    cJSON_AddNumberToObject(root, "zawal_buffer_min", cfg.zawal_buffer_min);
    cJSON_AddBoolToObject(root, "auto_brightness", cfg.auto_brightness);
    cJSON_AddBoolToObject(root, "night_lamp_enable", cfg.night_lamp_enable);

    cJSON *iqamah = cJSON_AddArrayToObject(root, "iqamah_offsets");
    cJSON *adhan_offsets = cJSON_AddArrayToObject(root, "adhan_offsets");
    for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT; i++) {
        cJSON_AddItemToArray(iqamah, cJSON_CreateNumber(cfg.iqamah_offset_min[i]));
        cJSON_AddItemToArray(adhan_offsets, cJSON_CreateNumber(cfg.adhan_offset_min[i]));
    }

    /* 5.1a seasonal jamat config. */
    cJSON *jamat_arr = cJSON_AddArrayToObject(root, "jamat");
    for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "winter_anchor", cfg.jamat[i].winter.anchor);
        cJSON_AddNumberToObject(p, "winter_offset", cfg.jamat[i].winter.offset_min);
        cJSON_AddNumberToObject(p, "summer_anchor", cfg.jamat[i].summer.anchor);
        cJSON_AddNumberToObject(p, "summer_offset", cfg.jamat[i].summer.offset_min);
        cJSON_AddNumberToObject(p, "round_min",     cfg.jamat[i].round_min);
        cJSON_AddItemToArray(jamat_arr, p);
    }
    cJSON *season = cJSON_AddObjectToObject(root, "season");
    cJSON_AddNumberToObject(season, "summer_start_month", cfg.season.summer_start_month);
    cJSON_AddNumberToObject(season, "summer_start_day",   cfg.season.summer_start_day);
    cJSON_AddNumberToObject(season, "winter_start_month", cfg.season.winter_start_month);
    cJSON_AddNumberToObject(season, "winter_start_day",   cfg.season.winter_start_day);

    esp_err_t err = send_json(req, root);
    cJSON_Delete(root);
    return err;
}

/* Common timetable-override confirmation check, used by both POST /api/config
 * and POST /api/config/preset.
 *
 * Returns:
 *   ESP_OK                -- proceed (no partial user timetable, or override confirmed)
 *   ESP_ERR_INVALID_STATE -- 409 Conflict already sent; caller must return
 *   ESP_ERR_INVALID_ARG   -- 400 Bad Request already sent; caller must return
 *
 * #128 (R7c) fix: previously the callers treated any header-fetch error as
 * "header missing" (409), so a value over 7 chars (which triggers
 * ESP_ERR_HTTPD_RESULT_TRUNC) was reported as 409 rather than 400, and any
 * non-empty value ("no", "false") was accepted as confirmation. Now truncation
 * and bogus values are 400; only "true", "yes", or "1" confirm. */
static esp_err_t require_timetable_override_if_needed(httpd_req_t *req)
{
    svc_timetable_metadata_t meta = {0};
    if (svc_timetable_get_metadata(&meta) != ESP_OK
        || !meta.file_present
        || meta.source != SVC_TIMETABLE_SOURCE_USER
        || !meta.partial) {
        return ESP_OK;
    }

    /* Buffer fits the longest accepted value ("true" = 4) plus NUL. A value
       over 7 chars triggers ESP_ERR_HTTPD_RESULT_TRUNC, which we reject as
       invalid (400) rather than treat as missing (409). */
    char hdr[8];
    esp_err_t err = httpd_req_get_hdr_value_str(
        req, "X-Confirm-Timetable-Override", hdr, sizeof(hdr));

    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"warning\":\"user_timetable_partial\"}");
        return ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: bad X-Confirm-Timetable-Override: %s",
                 req->uri, esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "X-Confirm-Timetable-Override invalid");
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(hdr, "true") == 0 || strcmp(hdr, "yes") == 0 ||
        strcmp(hdr, "1") == 0) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "%s: rejected X-Confirm-Timetable-Override='%s'",
             req->uri, hdr);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "X-Confirm-Timetable-Override must be true/yes/1");
    return ESP_ERR_INVALID_ARG;
}

/* POST /api/config */
static esp_err_t post_config_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_FAIL;

    /* If a partial user timetable is present, require explicit confirm. */
    esp_err_t ovr_err = require_timetable_override_if_needed(req);
    if (ovr_err != ESP_OK) return ovr_err;

    char *buf = read_body(req, 4096, NULL);
    if (!buf) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    esp_err_t first_err = ESP_OK;

    /* Helper macro: log a setter failure, remember the first one, keep parsing. */
    #define APPLY(call) do { \
        esp_err_t _e = (call); \
        if (_e != ESP_OK) { \
            ESP_LOGW(TAG, "setter failed: %s -> %s", #call, esp_err_to_name(_e)); \
            if (first_err == ESP_OK) first_err = _e; \
        } \
    } while (0)

    /* Fetch a field, validate integral + range, apply. Absent field is a
       no-op. Present-but-invalid sets first_err and logs, matching APPLY. */
    #define APPLY_NUM(field, min, max, apply_expr) do { \
        cJSON *_it = cJSON_GetObjectItem(root, field); \
        if (_it) { \
            double v; \
            if (json_get_uint_range(_it, (min), (max), &v)) { \
                APPLY(apply_expr); \
            } else { \
                ESP_LOGW(TAG, "rejected %s: not integral in [%g,%g]", \
                         field, (double)(min), (double)(max)); \
                if (first_err == ESP_OK) first_err = ESP_ERR_INVALID_ARG; \
            } \
        } \
    } while (0)

    /* Strings */
    cJSON *loc_name = cJSON_GetObjectItem(root, "location_name");
    if (cJSON_IsString(loc_name)) {
        APPLY(svc_config_set_location_name(loc_name->valuestring));
    }

    /* Doubles */
    cJSON *lat = cJSON_GetObjectItem(root, "latitude");
    cJSON *lon = cJSON_GetObjectItem(root, "longitude");
    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        APPLY(svc_config_set_location(lat->valuedouble, lon->valuedouble));
    }

    /* Enums. Integral-and-range before any enum cast, so 1.5 and 999 are
       rejected instead of silently becoming 1 and 7. */
    APPLY_NUM("method", 0, (double)(SVC_CONFIG_METHOD_COUNT - 1),
              svc_config_set_method((svc_config_method_t)v));
    APPLY_NUM("madhab", 0, (double)(SVC_CONFIG_MADHAB_COUNT - 1),
              svc_config_set_madhab((svc_config_madhab_t)v));
    APPLY_NUM("adhan_mode", 0, (double)(SVC_CONFIG_ADHAN_MODE_COUNT - 1),
              svc_config_set_adhan_mode((svc_config_adhan_mode_t)v));

    /* Custom angles are doubles in [0, 30]; no int cast, so the shared
       helper does not apply. Both must be present and in range together. */
    {
        cJSON *c_fajr = cJSON_GetObjectItem(root, "custom_fajr_angle");
        cJSON *c_isha = cJSON_GetObjectItem(root, "custom_isha_angle");
        if (cJSON_IsNumber(c_fajr) && cJSON_IsNumber(c_isha)) {
            double fj = c_fajr->valuedouble;
            double is = c_isha->valuedouble;
            if (fj >= 0.0 && fj <= 30.0 && is >= 0.0 && is <= 30.0) {
                APPLY(svc_config_set_custom_angles(fj, is));
            } else {
                ESP_LOGW(TAG, "rejected custom angles: out of [0,30]");
                if (first_err == ESP_OK) first_err = ESP_ERR_INVALID_ARG;
            }
        }
    }

    APPLY_NUM("high_lat_rule", 0, 4,
              svc_config_set_high_lat_rule((uint8_t)v));
    APPLY_NUM("zawal_buffer_min", 0, 30,
              svc_config_set_zawal_buffer((uint16_t)v));
    APPLY_NUM("adhan_volume", 0, 100,
              svc_config_set_adhan_volume((uint8_t)v));
    APPLY_NUM("brightness_mode", 0, 2,
              svc_config_set_brightness_mode((uint8_t)v));

    /* Bools */
    cJSON *auto_bright = cJSON_GetObjectItem(root, "auto_brightness");
    if (cJSON_IsBool(auto_bright)) {
        APPLY(svc_config_set_auto_brightness(cJSON_IsTrue(auto_bright)));
    }
    cJSON *night_lamp = cJSON_GetObjectItem(root, "night_lamp_enable");
    if (cJSON_IsBool(night_lamp)) {
        APPLY(svc_config_set_night_lamp(cJSON_IsTrue(night_lamp)));
    }

    /* Arrays — each element is integral-and-range checked before the cast,
       so a wrap like 65536+N -> N is rejected. The setter's own range
       check (0..1440 iqamah, 0..60 adhan) still runs; the helper rejects
       anything the uint16_t cast would have silently mangled. */
    cJSON *iqamah = cJSON_GetObjectItem(root, "iqamah_offsets");
    if (cJSON_IsArray(iqamah)) {
        int n = cJSON_GetArraySize(iqamah);
        for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT && i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(iqamah, i);
            if (!item) continue;
            double v;
            if (json_get_uint_range(item, 0, 1440, &v)) {
                APPLY(svc_config_set_iqamah_offset((svc_config_prayer_t)i, (uint16_t)v));
            } else {
                ESP_LOGW(TAG, "rejected iqamah_offsets[%d]: not integral in [0,1440]", i);
                if (first_err == ESP_OK) first_err = ESP_ERR_INVALID_ARG;
            }
        }
    }
    cJSON *adhan_off = cJSON_GetObjectItem(root, "adhan_offsets");
    if (cJSON_IsArray(adhan_off)) {
        int n = cJSON_GetArraySize(adhan_off);
        for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT && i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(adhan_off, i);
            if (!item) continue;
            double v;
            if (json_get_uint_range(item, 0, 60, &v)) {
                APPLY(svc_config_set_adhan_offset((svc_config_prayer_t)i, (uint16_t)v));
            } else {
                ESP_LOGW(TAG, "rejected adhan_offsets[%d]: not integral in [0,60]", i);
                if (first_err == ESP_OK) first_err = ESP_ERR_INVALID_ARG;
            }
        }
    }

    /* 5.1b: jamat[] array. Each entry: {winter_anchor, winter_offset,
       summer_anchor, summer_offset, round_min}. Server-side setters
       (svc_config_set_jamat_season / _set_jamat_round) enforce range
       and anchor-per-prayer validity; on failure the APPLY macro logs
       and continues, and a non-OK first_err returns 500. */
    cJSON *jamat_arr = cJSON_GetObjectItem(root, "jamat");
    if (cJSON_IsArray(jamat_arr)) {
        int n = cJSON_GetArraySize(jamat_arr);
        for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT && i < n; i++) {
            cJSON *j = cJSON_GetArrayItem(jamat_arr, i);
            if (!cJSON_IsObject(j)) continue;

            cJSON *w_anchor = cJSON_GetObjectItem(j, "winter_anchor");
            cJSON *w_offset = cJSON_GetObjectItem(j, "winter_offset");
            cJSON *s_anchor = cJSON_GetObjectItem(j, "summer_anchor");
            cJSON *s_offset = cJSON_GetObjectItem(j, "summer_offset");
            cJSON *round_m  = cJSON_GetObjectItem(j, "round_min");

            double av, ov;
            if (w_anchor && w_offset
                && json_get_uint_range(w_anchor, 0,
                                       (double)(SVC_CONFIG_JAMAT_ANCHOR_COUNT - 1), &av)
                && json_get_uint_range(w_offset, 0, 1440, &ov)) {
                APPLY(svc_config_set_jamat_season(
                    (svc_config_prayer_t)i, 0,
                    (svc_config_jamat_anchor_t)(int)av,
                    (uint16_t)ov));
            } else if (w_anchor || w_offset) {
                ESP_LOGW(TAG, "rejected jamat[%d].winter_*: not integral or out of range", i);
                if (first_err == ESP_OK) first_err = ESP_ERR_INVALID_ARG;
            }

            if (s_anchor && s_offset
                && json_get_uint_range(s_anchor, 0,
                                       (double)(SVC_CONFIG_JAMAT_ANCHOR_COUNT - 1), &av)
                && json_get_uint_range(s_offset, 0, 1440, &ov)) {
                APPLY(svc_config_set_jamat_season(
                    (svc_config_prayer_t)i, 1,
                    (svc_config_jamat_anchor_t)(int)av,
                    (uint16_t)ov));
            } else if (s_anchor || s_offset) {
                ESP_LOGW(TAG, "rejected jamat[%d].summer_*: not integral or out of range", i);
                if (first_err == ESP_OK) first_err = ESP_ERR_INVALID_ARG;
            }

            if (round_m) {
                double rv;
                if (json_get_uint_range(round_m, 0, 30, &rv)) {
                    /* The setter rejects values not in {0,2,5,10,15,20,30};
                       it is the authoritative validity check for round_min. */
                    APPLY(svc_config_set_jamat_round(
                        (svc_config_prayer_t)i, (uint8_t)rv));
                } else {
                    ESP_LOGW(TAG, "rejected jamat[%d].round_min: not integral in [0,30]", i);
                    if (first_err == ESP_OK) first_err = ESP_ERR_INVALID_ARG;
                }
            }
        }
    }

    /* 5.1b: season object with the two transition dates. */
    cJSON *season_obj = cJSON_GetObjectItem(root, "season");
    if (cJSON_IsObject(season_obj)) {
        cJSON *sm = cJSON_GetObjectItem(season_obj, "summer_start_month");
        cJSON *sd = cJSON_GetObjectItem(season_obj, "summer_start_day");
        cJSON *wm = cJSON_GetObjectItem(season_obj, "winter_start_month");
        cJSON *wd = cJSON_GetObjectItem(season_obj, "winter_start_day");
        double smv, sdv, wmv, wdv;
        if (sm && sd && wm && wd
            && json_get_uint_range(sm, 1, 12, &smv)
            && json_get_uint_range(sd, 1, 31, &sdv)
            && json_get_uint_range(wm, 1, 12, &wmv)
            && json_get_uint_range(wd, 1, 31, &wdv)) {
            APPLY(svc_config_set_season(
                (uint8_t)smv, (uint8_t)sdv,
                (uint8_t)wmv, (uint8_t)wdv));
        } else if (sm || sd || wm || wd) {
            ESP_LOGW(TAG, "rejected season: not integral or out of [1,12]/[1,31]");
            if (first_err == ESP_OK) first_err = ESP_ERR_INVALID_ARG;
        }
    }

    cJSON_Delete(root);
    #undef APPLY_NUM
    #undef APPLY

    /* #128 (R7a): the setters above apply as they are parsed, so on a partial
       failure earlier fields are already persisted. A full validate-then-apply
       refactor is out of scope; instead, (a) always recalculate so the display
       reflects the actual persisted state, (b) return 400 (client error) rather
       than 500 (server error), and (c) surface partial_apply in the body so
       a client can distinguish "nothing applied" from "some applied, some
       rejected". */
    svc_prayer_engine_service_recalculate();
    if (first_err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    } else {
        ESP_LOGW(TAG, "config apply partial: first_err=%s",
                 esp_err_to_name(first_err));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        char err_body[96];
        snprintf(err_body, sizeof(err_body),
                 "{\"error\":\"partial_apply\",\"detail\":\"%s\"}",
                 esp_err_to_name(first_err));
        httpd_resp_sendstr(req, err_body);
    }
    return first_err;
}

/* POST /api/wifi */
static esp_err_t post_wifi_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_FAIL;

    char *buf = read_body(req, 256, NULL);
    if (!buf) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "password");
    esp_err_t err = ESP_FAIL;
    if (cJSON_IsString(ssid) && cJSON_IsString(pass)) {
        err = svc_network_set_credentials(ssid->valuestring, pass->valuestring);
    }
    cJSON_Delete(root);

    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to save credentials");
    }
    return err;
}

/* POST /api/reboot */
static esp_err_t post_reboot_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_FAIL;

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    ESP_LOGW(TAG, "Reboot requested via API");
    vTaskDelay(pdMS_TO_TICKS(500)); // Give time for HTTP response to send
    esp_restart();
    return ESP_OK;
}

/* POST /api/factory-reset
 *
 * Writes a one-shot factory-reset request to NVS and reboots. The actual
 * wipe (NVS erase + /storage partition erase) runs in boot_manager_init()
 * on the next boot, where all the provisioning code lives. That keeps the
 * reset logic in one place and avoids svc_http duplicating HAL/filesystem
 * work. #129 R3 changed the storage wipe from mount-and-format-if-needed
 * to a direct esp_partition_erase_range() so a cleanly-mountable FS is
 * still erased.
 *
 * SECURITY: like /api/reboot and every other state-changing endpoint,
 * this handler calls require_auth() first, so a request needs
 * X-Chronolink-Auth: Bearer <device token>. That closes the CSRF path
 * from a browser page on the same WiFi. The AP setup network itself
 * remains open, so first-boot provisioning still works; the installer
 * pastes the token from the serial console once. */
static esp_err_t post_factory_reset_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_FAIL;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_BOOT_REQUEST, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(boot_req) failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "NVS unavailable");
        return ESP_FAIL;
    }

    err = nvs_set_u8(h, NVS_KEY_BOOT_REQUEST, NVS_BOOT_REQUEST_FACTORY_RESET);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to store boot request: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to store request");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    ESP_LOGW(TAG, "Factory reset requested via API; rebooting");
    vTaskDelay(pdMS_TO_TICKS(500));  /* let the HTTP response flush */
    esp_restart();
    return ESP_OK;
}

/* POST /api/ota
 *
 * Raw-body firmware uploader. Streams a .bin from the request body to
 * the inactive OTA slot in OTA_CHUNK_SIZE chunks, verifies the image via
 * esp_ota_end(), points the bootloader at the new slot, and reboots. On
 * any failure calls esp_ota_abort() and leaves the running image
 * untouched.
 *
 * Security: two checks, in order.
 *
 *   1. require_auth() demands X-Chronolink-Auth: Bearer <device token>.
 *      The device token lives in NVS, is printed once at first boot, and
 *      is NOT visible in the firmware binary. This closes the CSRF path
 *      from any browser on the same WiFi.
 *   2. X-OTA-Token must match the compile-time OTA_SHARED_TOKEN. That
 *      shared secret is visible in the binary, so it is defence in depth
 *      behind the device token, not the primary gate.
 *
 * Step 0 additionally refuses OTA entirely while OTA_SHARED_TOKEN is
 * still the shipped default ("change-me-before-flashing").
 *
 * Rollback safety: relies on PR 4.6. If the new image crashes before
 * the running image marks itself valid at the end of app_main() (see
 * main/main.c, #129 R2), the bootloader reverts to the current slot on
 * the next reset. The build-time #error in this file guarantees
 * rollback is enabled. */
static esp_err_t post_ota_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_FAIL;

    /* 0. Refuse OTA entirely while the shared token is still the shipped
       default. The compile-time default is in the public source tree, so it
       offers no protection; enabling OTA on a default token would let any
       LAN client flash arbitrary firmware. The installer must set a real
       token via -DOTA_SHARED_TOKEN=... at build time. */
    if (strcmp(OTA_SHARED_TOKEN, "change-me-before-flashing") == 0) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"ota_disabled\"}");
        ESP_LOGW(TAG, "OTA rejected: shared token still set to default");
        return ESP_FAIL;
    }

    /* 1. Token check (constant-time-ish strcmp; fine for this threat model) */
    char token_buf[64];
    if (httpd_req_get_hdr_value_str(req, "X-OTA-Token", token_buf, sizeof(token_buf)) != ESP_OK
        || strcmp(token_buf, OTA_SHARED_TOKEN) != 0) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Bad or missing X-OTA-Token");
        ESP_LOGW(TAG, "OTA rejected: bad or missing token");
        return ESP_FAIL;
    }

    /* 2. Content-Length sanity */
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA slot");
        return ESP_FAIL;
    }
    if ((uint32_t)req->content_len > next->size) {
        ESP_LOGW(TAG, "OTA rejected: content_len=%d > slot size=%lu",
                 (int)req->content_len, (unsigned long)next->size);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image larger than OTA slot");
        return ESP_FAIL;
    }

    /* 3. Begin (sequential writes: no whole-image buffering) */
    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    /* 4. Stream: recv -> write in OTA_CHUNK_SIZE chunks */
    char *buf = malloc(OTA_CHUNK_SIZE);
    if (!buf) {
        esp_ota_abort(handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int total_written = 0;
    err = ESP_OK;

    int consecutive_timeouts = 0;
    while (remaining > 0) {
        int to_recv = (remaining < OTA_CHUNK_SIZE) ? remaining : OTA_CHUNK_SIZE;
        int received = httpd_req_recv(req, buf, to_recv);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++consecutive_timeouts >= OTA_MAX_CONSECUTIVE_TIMEOUTS) {
                ESP_LOGW(TAG, "OTA aborted: client stalled for %d consecutive recvs",
                         consecutive_timeouts);
                err = ESP_ERR_TIMEOUT;
                break;
            }
            continue;
        }
        consecutive_timeouts = 0;
        if (received < 0) {
            ESP_LOGE(TAG, "httpd_req_recv failed: %d", received);
            err = ESP_FAIL;
            break;
        }
        if (received == 0) {
            ESP_LOGE(TAG, "OTA: peer closed early at %d/%d bytes",
                     total_written, (int)req->content_len);
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(handle, buf, (size_t)received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at %d/%d: %s",
                     total_written, (int)req->content_len, esp_err_to_name(err));
            break;
        }
        remaining -= received;
        total_written += received;
        if ((total_written % (OTA_CHUNK_SIZE * 8)) == 0) {
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes",
                     total_written, (int)req->content_len);
        }
    }
    free(buf);

    if (err != ESP_OK || remaining != 0) {
        esp_ota_abort(handle);
        if (err == ESP_ERR_TIMEOUT) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT,
                                "OTA upload timed out");
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Upload failed");
        }
        return ESP_FAIL;
    }

    /* 5. End: verifies image magic and app descriptor checksum */
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed (invalid image?): %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image invalid");
        return ESP_FAIL;
    }

    /* 6. Point the bootloader at the new slot */
    err = esp_ota_set_boot_partition(next);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot failed");
        return ESP_FAIL;
    }

    /* 7. Respond, wait for the response to flush, reboot */
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"bytes\":%d}", total_written);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);

    ESP_LOGW(TAG, "OTA complete: %d bytes written to %s, rebooting",
             total_written, next->label);
    vTaskDelay(pdMS_TO_TICKS(500));  /* let the HTTP response flush */
    esp_restart();
    return ESP_OK;
}

/* GET /api/prayers */
static esp_err_t get_prayers_handler(httpd_req_t *req)
{
    svc_prayer_engine_times_t times = {0};
    esp_err_t err = svc_prayer_engine_service_get_times(&times);
    
    cJSON *root = cJSON_CreateObject();
    if (err == ESP_OK && times.valid) {
        cJSON_AddBoolToObject(root, "valid", true);
        cJSON_AddNumberToObject(root, "season", times.season);
        cJSON_AddNumberToObject(root, "source", times.source);

        /* Nested three-tier shape (new in 5.1a): astronomical / adhan /
           jamat / auxiliary / ramadan. Consumers read finished values;
           no offset math in the UI. */

        cJSON *ast = cJSON_AddObjectToObject(root, "astronomical");
        cJSON_AddNumberToObject(ast, "fajr_sec",    times.astronomical_fajr_sec);
        cJSON_AddNumberToObject(ast, "dhuhr_sec",   times.astronomical_dhuhr_sec);
        cJSON_AddNumberToObject(ast, "asr_sec",     times.astronomical_asr_sec);
        cJSON_AddNumberToObject(ast, "maghrib_sec", times.astronomical_maghrib_sec);
        cJSON_AddNumberToObject(ast, "isha_sec",    times.astronomical_isha_sec);

        cJSON *adhan = cJSON_AddObjectToObject(root, "adhan");
        cJSON_AddNumberToObject(adhan, "fajr_sec",    times.fajr_sec);
        cJSON_AddNumberToObject(adhan, "dhuhr_sec",   times.dhuhr_sec);
        cJSON_AddNumberToObject(adhan, "asr_sec",     times.asr_sec);
        cJSON_AddNumberToObject(adhan, "maghrib_sec", times.maghrib_sec);
        cJSON_AddNumberToObject(adhan, "isha_sec",    times.isha_sec);

        cJSON *jamat = cJSON_AddObjectToObject(root, "jamat");
        cJSON *jamat_today    = cJSON_AddArrayToObject(jamat, "today_sec");
        cJSON *jamat_tomorrow = cJSON_AddArrayToObject(jamat, "tomorrow_sec");
        for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
            cJSON_AddItemToArray(jamat_today,
                times.jamat_sec[p] == UINT32_MAX
                    ? cJSON_CreateNull()
                    : cJSON_CreateNumber(times.jamat_sec[p]));
            cJSON_AddItemToArray(jamat_tomorrow,
                times.jamat_tomorrow_sec[p] == UINT32_MAX
                    ? cJSON_CreateNull()
                    : cJSON_CreateNumber(times.jamat_tomorrow_sec[p]));
        }

        cJSON *aux = cJSON_AddObjectToObject(root, "auxiliary");
        cJSON_AddNumberToObject(aux, "sunrise_sec",     times.sunrise_sec);
        cJSON_AddNumberToObject(aux, "sunset_sec",      times.sunset_sec);
        cJSON_AddNumberToObject(aux, "midnight_sec",    times.midnight_sec);
        cJSON_AddNumberToObject(aux, "zawal_start_sec", times.zawal_start_sec);
        cJSON_AddNumberToObject(aux, "asr_yellow_sec",  times.asr_yellow_sec);

        cJSON *ram = cJSON_AddObjectToObject(root, "ramadan");
        cJSON_AddNumberToObject(ram, "sehri_end_sec", times.sehri_end_sec);
        cJSON_AddNumberToObject(ram, "iftar_sec",     times.iftar_sec);

        /* Flat aliases for backward compatibility with pre-5.1a
           consumers. */
        cJSON_AddNumberToObject(root, "fajr_sec",         times.fajr_sec);
        cJSON_AddNumberToObject(root, "sunrise_sec",      times.sunrise_sec);
        cJSON_AddNumberToObject(root, "dhuhr_sec",        times.dhuhr_sec);
        cJSON_AddNumberToObject(root, "asr_sec",          times.asr_sec);
        cJSON_AddNumberToObject(root, "maghrib_sec",      times.maghrib_sec);
        cJSON_AddNumberToObject(root, "isha_sec",         times.isha_sec);
        cJSON_AddNumberToObject(root, "sehri_end_sec",    times.sehri_end_sec);
        cJSON_AddNumberToObject(root, "iftar_sec",        times.iftar_sec);

        cJSON *flat_jamat = cJSON_AddArrayToObject(root, "jamat_sec");
        cJSON *flat_jamat_tm = cJSON_AddArrayToObject(root, "jamat_tomorrow_sec");
        for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
            cJSON_AddItemToArray(flat_jamat,
                times.jamat_sec[p] == UINT32_MAX
                    ? cJSON_CreateNull()
                    : cJSON_CreateNumber(times.jamat_sec[p]));
            cJSON_AddItemToArray(flat_jamat_tm,
                times.jamat_tomorrow_sec[p] == UINT32_MAX
                    ? cJSON_CreateNull()
                    : cJSON_CreateNumber(times.jamat_tomorrow_sec[p]));
        }
    } else {
        cJSON_AddBoolToObject(root, "valid", false);
    }

    esp_err_t ret = send_json(req, root);
    cJSON_Delete(root);
    return ret;
}

/* POST /api/config/preset
 *
 * Body: {"preset": 0} for UK, {"preset": 1} for Pakistan,
 *       {"preset": 2} for UK (Moonsighting).
 * Applies the preset then triggers an immediate recalculation. */
static esp_err_t post_preset_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_FAIL;

    esp_err_t ovr_err = require_timetable_override_if_needed(req);
    if (ovr_err != ESP_OK) return ovr_err;

    char *buf = read_body(req, 128, NULL);
    if (!buf) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *preset = cJSON_GetObjectItem(root, "preset");
    esp_err_t err = ESP_ERR_INVALID_ARG;
    /* #128 (R7b): reject non-integral (1.5) and out-of-range before the
       enum cast; the previous valueint path silently truncated 1.5 -> 1.
       The upper bound tracks SVC_CONFIG_PRESET_COUNT automatically, so
       adding a fourth preset needs no change here. */
    double preset_v = 0.0;
    if (json_get_uint_range(preset, 0,
                            (double)(SVC_CONFIG_PRESET_COUNT - 1), &preset_v)) {
        err = svc_config_apply_preset((svc_config_preset_t)(int)preset_v);
    }
    cJSON_Delete(root);

    if (err == ESP_OK) {
        /* #132 (R4): apply the preset's timezone BEFORE recalculating,
           so the fresh prayer times and the timetable are expressed in
           the new zone. svc_time owns TZ persistence; this call live-
           applies and persists. Best-effort: the preset itself already
           succeeded, and svc_time_set_timezone_posix() logs internally
           on failure. */
        const char *preset_tz =
            svc_config_preset_timezone((svc_config_preset_t)(int)preset_v);
        if (preset_tz) {
            (void)svc_time_set_timezone_posix(preset_tz);
        }
        svc_prayer_engine_service_recalculate();
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid preset");
    }
    return err;
}

/* GET /api/timetable -- metadata only */
static esp_err_t get_timetable_handler(httpd_req_t *req)
{
    svc_timetable_metadata_t meta = {0};
    svc_timetable_get_metadata(&meta);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "present", meta.file_present);
    cJSON_AddNumberToObject(root, "source", meta.source);
    cJSON_AddNumberToObject(root, "year", meta.year);
    cJSON_AddNumberToObject(root, "day_count", meta.day_count);
    cJSON_AddStringToObject(root, "generated_at", meta.generated_at);
    cJSON_AddBoolToObject(root, "jamat_included", meta.jamat_included);
    cJSON_AddBoolToObject(root, "partial", meta.partial);
    cJSON_AddBoolToObject(root, "regenerating", svc_prayer_engine_service_is_regenerating());

    esp_err_t err = send_json(req, root);
    cJSON_Delete(root);
    return err;
}

/* POST /api/timetable/regenerate */
static esp_err_t post_timetable_regen_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_FAIL;

    esp_err_t err = svc_timetable_generate_full_year();
    if (err == ESP_OK) {
        svc_prayer_engine_service_recalculate();
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Regeneration failed");
    }
    return err;
}

/* DELETE /api/timetable */
static esp_err_t delete_timetable_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_FAIL;

    esp_err_t err = svc_timetable_delete();
    if (err == ESP_OK) {
        svc_prayer_engine_service_recalculate();
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delete failed");
    }
    return err;
}

/* POST /api/timetable -- upload user file */
static esp_err_t post_timetable_upload_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_FAIL;

    if (req->content_len <= 0 || req->content_len > 128 * 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or too-large body");
        return ESP_FAIL;
    }

    /* Serialise against the background regenerator and the manual regenerate
       endpoint. Without this, an upload that lands mid-regen can be silently
       overwritten when the regenerator finally renames its own temp file
       into place. Timed out -> 503, caller can retry. */
    if (svc_timetable_acquire_writer() != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "Timetable busy; retry");
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;

    FILE *f = fopen(SVC_TIMETABLE_UPLOAD_TMP, "w");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot open file");
        goto done;
    }

    char *buf = malloc(4096);
    if (!buf) {
        fclose(f); remove(SVC_TIMETABLE_UPLOAD_TMP);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        goto done;
    }

    int remaining = req->content_len;
    int consecutive_timeouts = 0;
    bool upload_failed = false;
    while (remaining > 0) {
        int to_recv = remaining < 4096 ? remaining : 4096;
        int received = httpd_req_recv(req, buf, to_recv);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++consecutive_timeouts >= UPLOAD_MAX_CONSECUTIVE_TIMEOUTS) {
                ESP_LOGW(TAG, "upload aborted: client stalled for %d consecutive recvs",
                         consecutive_timeouts);
                free(buf); fclose(f); remove(SVC_TIMETABLE_UPLOAD_TMP);
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Upload timed out");
                goto done;
            }
            continue;
        }
        consecutive_timeouts = 0;
        if (received < 0) {
            free(buf); fclose(f); remove(SVC_TIMETABLE_UPLOAD_TMP);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv failed");
            goto done;
        }
        if (received == 0) {
            /* Peer closed early. content_len said more was coming. */
            upload_failed = true;
            break;
        }
        size_t written = fwrite(buf, 1, (size_t)received, f);
        if (written != (size_t)received) {
            ESP_LOGE(TAG, "upload: fwrite short (%u of %d) -- disk full?",
                     (unsigned)written, received);
            free(buf); fclose(f); remove(SVC_TIMETABLE_UPLOAD_TMP);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            goto done;
        }
        remaining -= received;
    }

    if (upload_failed) {
        free(buf); fclose(f); remove(SVC_TIMETABLE_UPLOAD_TMP);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Upload short");
        goto done;
    }
    free(buf);
    fflush(f); fsync(fileno(f)); fclose(f);

    /* Validate the candidate file BEFORE replacing the live one. The
       validator parses the tmp file into private temporaries, so a bad file
       cannot corrupt the running state. A rejected upload leaves
       SVC_TIMETABLE_PATH untouched. */
    esp_err_t v_err = svc_timetable_validate(SVC_TIMETABLE_UPLOAD_TMP);
    if (v_err != ESP_OK) {
        remove(SVC_TIMETABLE_UPLOAD_TMP);
        const char *msg =
            (v_err == ESP_ERR_INVALID_VERSION)   ? "Unsupported schema_version"
          : (v_err == ESP_ERR_INVALID_RESPONSE)  ? "Malformed timetable JSON"
          : (v_err == ESP_ERR_INVALID_ARG)       ? "Bad upload path"
          :                                        "Timetable validation failed";
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        goto done;
    }

    if (rename(SVC_TIMETABLE_UPLOAD_TMP, SVC_TIMETABLE_PATH) != 0) {
        static const char *BAK = "/storage/timetable.bak";
        remove(BAK);
        bool have_backup = (rename(SVC_TIMETABLE_PATH, BAK) == 0);
        if (rename(SVC_TIMETABLE_UPLOAD_TMP, SVC_TIMETABLE_PATH) != 0) {
            if (have_backup) {
                (void)rename(BAK, SVC_TIMETABLE_PATH);
            }
            remove(SVC_TIMETABLE_UPLOAD_TMP);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Rename failed");
            goto done;
        }
        if (have_backup) {
            remove(BAK);
        }
    }

    svc_timetable_reload();
    svc_prayer_engine_service_recalculate();
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    result = ESP_OK;

done:
    svc_timetable_release_writer();
    return result;
}

#if defined(CONFIG_CHRONOLINK_TEST_HOOKS) || defined(CHRONOLINK_TEST_HOOKS)
/* ------------------------------------------------------------------ */
/* Test hooks -- compiled only in [env:single_mcu_test]               */
/* ------------------------------------------------------------------ */

/* svc_time's own limit is SVC_TIME_NTP_HOST_MAX (64) but that macro is
 * private to svc_time.c; anything at or above 64 is rejected by
 * svc_time_set_ntp_server() with ESP_ERR_INVALID_ARG, reported as a 500. */
#define DEBUG_NTP_HOST_MAX   64
#define DEBUG_NTP_QUERY_MAX  96

/* GET /debug/ntp?host=<ip-or-hostname>
 *
 * Test-harness hook (test_harness/): point NTP slot 0 at the PC running the
 * mock NTP server and force one sync, so a scenario can drag the device to an
 * arbitrary date in seconds. The production env does not define
 * -DCHRONOLINK_TEST_HOOKS=1, so neither this handler nor the "/debug/ntp"
 * string exist in that image (verify with `strings ... | findstr debug/ntp`).
 *
 * Unauthenticated on purpose: the harness runs on a closed LAN. Never ship.
 *
 * AM7: svc_time_set_ntp_server() persists slot 0 to NVS. svc_time offers no
 * transient setter, so the write is accepted; restore with
 * `pio run -e single_mcu -t erase` or by pointing slot 0 back at a pool host
 * through this same endpoint.
 *
 * AM8 / AM10: svc_time_sync_ntp_once() blocks in this task for up to
 * SVC_TIME_SYNC_WAIT_MS (10 s). Clients must allow a timeout above that;
 * device_controller.py uses 15 s.
 */
static esp_err_t get_debug_ntp_handler(httpd_req_t *req)
{
    char query[DEBUG_NTP_QUERY_MAX];
    char host[DEBUG_NTP_HOST_MAX];
    char body[192];
    time_t epoch = 0;
    esp_err_t err;

    err = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "debug/ntp: no query string: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"reason\":\"missing_query\"}");
        return ESP_OK;
    }

    err = httpd_query_key_value(query, "host", host, sizeof(host));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "debug/ntp: no host parameter: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"reason\":\"missing_host\"}");
        return ESP_OK;
    }

    /* httpd_query_key_value() does not URL-decode. The harness sends a plain
     * host or dotted-quad address, so a '%' means the caller assumed
     * decoding -- reject it instead of handing svc_time a mangled host. */
    if (host[0] == '\0' || strchr(host, '%') != NULL) {
        ESP_LOGW(TAG, "debug/ntp: rejected host '%.*s'", (int)sizeof(host), host);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"reason\":\"bad_host\"}");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "debug/ntp: TEST HOOK -- NTP slot 0 -> '%s'", host);

    err = svc_time_set_ntp_server(0, host);
    if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
        /* RAM slot 0 is live but the value will not survive a reboot. */
        ESP_LOGE(TAG, "debug/ntp: NVS unavailable, '%s' not persisted", host);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"reason\":\"nvs_unavailable\"}");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "debug/ntp: svc_time_set_ntp_server failed: %s",
                 esp_err_to_name(err));
        snprintf(body, sizeof(body),
                 "{\"status\":\"error\",\"reason\":\"set_ntp_server_failed\",\"detail\":\"%s\"}",
                 esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, body);
        return ESP_OK;
    }

    err = svc_time_sync_ntp_once();
    if (err == ESP_ERR_NOT_FINISHED) {
        /* #130 R9c follow-up: the previous sync is still fresh (<60 s), so
           svc_time_sync_ntp_once() returned ESP_ERR_NOT_FINISHED instead of
           running. The HIL harness hits this on every scenario (set_ntp_host
           runs right after the auto post-DHCP sync); a 500 here aborts the
           run. Report the skip honestly with a 200 so the harness can tell
           "skipped, clock already fresh" from a real failure. */
        (void)svc_time_get_epoch(&epoch);
        ESP_LOGI(TAG, "debug/ntp: sync skipped, previous sync still fresh");
        snprintf(body, sizeof(body),
                 "{\"status\":\"skipped\",\"host\":\"%s\",\"reason\":\"fresh\",\"epoch\":%lld}",
                 host, (long long)epoch);
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, body);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "debug/ntp: svc_time_sync_ntp_once failed: %s",
                 esp_err_to_name(err));
        snprintf(body, sizeof(body),
                 "{\"status\":\"error\",\"reason\":\"sync_failed\",\"detail\":\"%s\"}",
                 esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, body);
        return ESP_OK;
    }

    (void)svc_time_get_epoch(&epoch);
    ESP_LOGI(TAG, "debug/ntp: synced from '%s', epoch %lld", host, (long long)epoch);
    snprintf(body, sizeof(body),
             "{\"status\":\"ok\",\"host\":\"%s\",\"epoch\":%lld}",
             host, (long long)epoch);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}
#endif /* CONFIG_CHRONOLINK_TEST_HOOKS || CHRONOLINK_TEST_HOOKS */

esp_err_t svc_http_start(void)
{
    if (s_server) return ESP_OK;

    esp_err_t tok_err = load_or_generate_api_token();
    if (tok_err != ESP_OK) {
        ESP_LOGE(TAG, "refusing to start HTTP without an API token (%s)",
                 esp_err_to_name(tok_err));
        return tok_err;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    /* 13 production routes + the /debug/ntp test hook (test env only)
     * + headroom. */
    config.max_uri_handlers = 16;
    /* OTA handler calls esp_ota_* which needs more stack than a JSON
     * handler. 8192 is conservative; default 4096 has no margin for a
     * streaming upload with abort paths. */
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    if (strcmp(OTA_SHARED_TOKEN, "change-me-before-flashing") == 0) {
        ESP_LOGW(TAG, "OTA disabled: OTA_SHARED_TOKEN is the default. "
                      "Set -DOTA_SHARED_TOKEN=... at build time to enable.");
    }

    const httpd_uri_t handlers[] = {
        { .uri = "/",             .method = HTTP_GET,  .handler = get_html_handler      },
        { .uri = "/api/config",   .method = HTTP_GET,  .handler = get_config_handler    },
        { .uri = "/api/config",         .method = HTTP_POST, .handler = post_config_handler   },
        { .uri = "/api/config/preset",  .method = HTTP_POST, .handler = post_preset_handler   },
        { .uri = "/api/wifi",     .method = HTTP_POST, .handler = post_wifi_handler     },
        { .uri = "/api/reboot",        .method = HTTP_POST, .handler = post_reboot_handler        },
        { .uri = "/api/factory-reset", .method = HTTP_POST, .handler = post_factory_reset_handler },
        { .uri = "/api/ota",           .method = HTTP_POST, .handler = post_ota_handler            },
        { .uri = "/api/timetable",            .method = HTTP_GET,    .handler = get_timetable_handler         },
        { .uri = "/api/timetable",            .method = HTTP_POST,   .handler = post_timetable_upload_handler },
        { .uri = "/api/timetable",            .method = HTTP_DELETE, .handler = delete_timetable_handler      },
        { .uri = "/api/timetable/regenerate", .method = HTTP_POST,   .handler = post_timetable_regen_handler  },
        { .uri = "/api/prayers",       .method = HTTP_GET,  .handler = get_prayers_handler        },
#if defined(CONFIG_CHRONOLINK_TEST_HOOKS) || defined(CHRONOLINK_TEST_HOOKS)
        { .uri = "/debug/ntp",   .method = HTTP_GET,  .handler = get_debug_ntp_handler },
#endif
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        esp_err_t r = httpd_register_uri_handler(s_server, &handlers[i]);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", handlers[i].uri, esp_err_to_name(r));
        }
    }

    ESP_LOGI(TAG, "HTTP Server started on port 80");
    return ESP_OK;
}
