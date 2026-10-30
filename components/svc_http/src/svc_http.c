#include "svc_http.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "svc_config.h"
#include "svc_network.h"
#include "svc_prayer_engine_service.h"
#include "svc_timetable.h"
#include "esp_system.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

/* Compile-time gate: POST /api/ota must not ship without rollback
 * enabled. CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y (PR 4.6) makes a
 * failed OTA image revert automatically. Without it, a bad image that
 * crashes before boot_manager marks it valid would brick the device. */
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

extern const unsigned char index_html[];
extern const size_t index_html_len;
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>   /* fsync(), fileno() for the timetable upload */

static const char *TAG = "svc_http";
static httpd_handle_t s_server = NULL;

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

/* POST /api/config */
static esp_err_t post_config_handler(httpd_req_t *req)
{
    /* If a partial user timetable is present, require explicit confirm. */
    svc_timetable_metadata_t _meta = {0};
    if (svc_timetable_get_metadata(&_meta) == ESP_OK
        && _meta.file_present
        && _meta.source == SVC_TIMETABLE_SOURCE_USER
        && _meta.partial) {
        char _hdr[8];
        if (httpd_req_get_hdr_value_str(req, "X-Confirm-Timetable-Override",
                                        _hdr, sizeof(_hdr)) != ESP_OK) {
            httpd_resp_set_status(req, "409 Conflict");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"warning\":\"user_timetable_partial\"}");
            return ESP_ERR_INVALID_STATE;
        }
    }

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

    /* Enums */
    cJSON *method = cJSON_GetObjectItem(root, "method");
    if (cJSON_IsNumber(method)) {
        APPLY(svc_config_set_method((svc_config_method_t)method->valueint));
    }
    cJSON *madhab = cJSON_GetObjectItem(root, "madhab");
    if (cJSON_IsNumber(madhab)) {
        APPLY(svc_config_set_madhab((svc_config_madhab_t)madhab->valueint));
    }
    cJSON *c_fajr = cJSON_GetObjectItem(root, "custom_fajr_angle");
    cJSON *c_isha = cJSON_GetObjectItem(root, "custom_isha_angle");
    if (cJSON_IsNumber(c_fajr) && cJSON_IsNumber(c_isha)) {
        APPLY(svc_config_set_custom_angles(c_fajr->valuedouble, c_isha->valuedouble));
    }

    cJSON *hl = cJSON_GetObjectItem(root, "high_lat_rule");
    if (cJSON_IsNumber(hl)) {
        APPLY(svc_config_set_high_lat_rule((uint8_t)hl->valueint));
    }
    cJSON *zawal = cJSON_GetObjectItem(root, "zawal_buffer_min");
    if (cJSON_IsNumber(zawal)) {
        APPLY(svc_config_set_zawal_buffer((uint16_t)zawal->valueint));
    }
    cJSON *adhan_mode = cJSON_GetObjectItem(root, "adhan_mode");
    if (cJSON_IsNumber(adhan_mode)) {
        APPLY(svc_config_set_adhan_mode((svc_config_adhan_mode_t)adhan_mode->valueint));
    }
    cJSON *adhan_vol = cJSON_GetObjectItem(root, "adhan_volume");
    if (cJSON_IsNumber(adhan_vol)) {
        APPLY(svc_config_set_adhan_volume((uint8_t)adhan_vol->valueint));
    }
    cJSON *brightness = cJSON_GetObjectItem(root, "brightness_mode");
    if (cJSON_IsNumber(brightness)) {
        APPLY(svc_config_set_brightness_mode((uint8_t)brightness->valueint));
    }

    /* Bools */
    cJSON *auto_bright = cJSON_GetObjectItem(root, "auto_brightness");
    if (cJSON_IsBool(auto_bright)) {
        APPLY(svc_config_set_auto_brightness(cJSON_IsTrue(auto_bright)));
    }
    cJSON *night_lamp = cJSON_GetObjectItem(root, "night_lamp_enable");
    if (cJSON_IsBool(night_lamp)) {
        APPLY(svc_config_set_night_lamp(cJSON_IsTrue(night_lamp)));
    }

    /* Arrays — each element must be checked before use */
    cJSON *iqamah = cJSON_GetObjectItem(root, "iqamah_offsets");
    if (cJSON_IsArray(iqamah)) {
        for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT && i < cJSON_GetArraySize(iqamah); i++) {
            cJSON *item = cJSON_GetArrayItem(iqamah, i);
            if (cJSON_IsNumber(item)) {
                APPLY(svc_config_set_iqamah_offset((svc_config_prayer_t)i, (uint16_t)item->valueint));
            }
        }
    }
    cJSON *adhan_off = cJSON_GetObjectItem(root, "adhan_offsets");
    if (cJSON_IsArray(adhan_off)) {
        for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT && i < cJSON_GetArraySize(adhan_off); i++) {
            cJSON *item = cJSON_GetArrayItem(adhan_off, i);
            if (cJSON_IsNumber(item)) {
                APPLY(svc_config_set_adhan_offset((svc_config_prayer_t)i, (uint16_t)item->valueint));
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

            if (cJSON_IsNumber(w_anchor) && cJSON_IsNumber(w_offset)) {
                APPLY(svc_config_set_jamat_season(
                    (svc_config_prayer_t)i, 0,
                    (svc_config_jamat_anchor_t)w_anchor->valueint,
                    (uint16_t)w_offset->valueint));
            }
            if (cJSON_IsNumber(s_anchor) && cJSON_IsNumber(s_offset)) {
                APPLY(svc_config_set_jamat_season(
                    (svc_config_prayer_t)i, 1,
                    (svc_config_jamat_anchor_t)s_anchor->valueint,
                    (uint16_t)s_offset->valueint));
            }
            if (cJSON_IsNumber(round_m)) {
                APPLY(svc_config_set_jamat_round(
                    (svc_config_prayer_t)i,
                    (uint8_t)round_m->valueint));
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
        if (cJSON_IsNumber(sm) && cJSON_IsNumber(sd) &&
            cJSON_IsNumber(wm) && cJSON_IsNumber(wd)) {
            APPLY(svc_config_set_season(
                (uint8_t)sm->valueint, (uint8_t)sd->valueint,
                (uint8_t)wm->valueint, (uint8_t)wd->valueint));
        }
    }

    cJSON_Delete(root);
    #undef APPLY

    if (first_err == ESP_OK) {
        /* Force an immediate recalculation so the UI reflects new times instantly */
        svc_prayer_engine_service_recalculate();
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "One or more setters failed");
    }
    return first_err;
}

/* POST /api/wifi */
static esp_err_t post_wifi_handler(httpd_req_t *req)
{
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
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    ESP_LOGW(TAG, "Reboot requested via API");
    vTaskDelay(pdMS_TO_TICKS(500)); // Give time for HTTP response to send
    esp_restart();
    return ESP_OK;
}

/* POST /api/factory-reset
 *
 * Writes a one-shot factory-reset request to NVS and reboots. The actual
 * wipe (NVS erase + FATFS reformat) runs in boot_manager_init() on the
 * next boot, where all the provisioning code lives. That keeps the reset
 * logic in one place and avoids svc_http duplicating HAL/filesystem work.
 *
 * SECURITY NOTE: like /api/reboot, this endpoint has no authentication.
 * In AP mode the network is open and the device is reachable by anyone
 * on the setup SSID. That is consistent with /api/wifi and /api/config
 * today; a token-based auth scheme is a future PR, not this one. */
static esp_err_t post_factory_reset_handler(httpd_req_t *req)
{
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
 * Security: requires the X-OTA-Token header to match OTA_SHARED_TOKEN
 * (compile-time). This removes the drive-by-flashing risk from the open
 * AP setup network. The token is visible to anyone who can read the
 * firmware binary -- acceptable for a home device; a stronger scheme
 * (per-device NVS token) is a future PR.
 *
 * Rollback safety: relies on PR 4.6. If the new image crashes before
 * boot_manager_init() reaches esp_ota_mark_app_valid_cancel_rollback(),
 * the bootloader reverts to the current slot on the next reset. The
 * build-time #error in this file guarantees that. */
static esp_err_t post_ota_handler(httpd_req_t *req)
{
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

    while (remaining > 0) {
        int to_recv = (remaining < OTA_CHUNK_SIZE) ? remaining : OTA_CHUNK_SIZE;
        int received = httpd_req_recv(req, buf, to_recv);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            /* Not fatal: the client is slow, try again. */
            continue;
        }
        if (received < 0) {
            ESP_LOGE(TAG, "httpd_req_recv failed: %d", received);
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
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
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
    svc_timetable_metadata_t _meta = {0};
    if (svc_timetable_get_metadata(&_meta) == ESP_OK
        && _meta.file_present
        && _meta.source == SVC_TIMETABLE_SOURCE_USER
        && _meta.partial) {
        char _hdr[8];
        if (httpd_req_get_hdr_value_str(req, "X-Confirm-Timetable-Override",
                                        _hdr, sizeof(_hdr)) != ESP_OK) {
            httpd_resp_set_status(req, "409 Conflict");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"warning\":\"user_timetable_partial\"}");
            return ESP_ERR_INVALID_STATE;
        }
    }

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
    if (cJSON_IsNumber(preset)) {
        err = svc_config_apply_preset((svc_config_preset_t)preset->valueint);
    }
    cJSON_Delete(root);

    if (err == ESP_OK) {
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
    if (req->content_len <= 0 || req->content_len > 512 * 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or too-large body");
        return ESP_FAIL;
    }

    FILE *f = fopen(SVC_TIMETABLE_PATH_TMP, "w");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot open file");
        return ESP_FAIL;
    }

    char *buf = malloc(4096);
    if (!buf) {
        fclose(f); remove(SVC_TIMETABLE_PATH_TMP);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    while (remaining > 0) {
        int to_recv = remaining < 4096 ? remaining : 4096;
        int received = httpd_req_recv(req, buf, to_recv);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received < 0) {
            free(buf); fclose(f); remove(SVC_TIMETABLE_PATH_TMP);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv failed");
            return ESP_FAIL;
        }
        fwrite(buf, 1, received, f);
        remaining -= received;
    }
    free(buf);
    fflush(f); fsync(fileno(f)); fclose(f);

    if (rename(SVC_TIMETABLE_PATH_TMP, SVC_TIMETABLE_PATH) != 0) {
        remove(SVC_TIMETABLE_PATH_TMP);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Rename failed");
        return ESP_FAIL;
    }

    svc_timetable_reload();
    svc_prayer_engine_service_recalculate();
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t svc_http_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 14;
    /* OTA handler calls esp_ota_* which needs more stack than a JSON
     * handler. 8192 is conservative; default 4096 has no margin for a
     * streaming upload with abort paths. */
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
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
