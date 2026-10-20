#include "svc_http.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "svc_config.h"
#include "svc_network.h"
#include "svc_prayer_engine_service.h"
#include "esp_system.h"
#include "nvs.h"

extern const unsigned char index_html[];
extern const size_t index_html_len;
#include <string.h>
#include <stdlib.h>

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

    esp_err_t err = send_json(req, root);
    cJSON_Delete(root);
    return err;
}

/* POST /api/config */
static esp_err_t post_config_handler(httpd_req_t *req)
{
    char *buf = read_body(req, 2048, NULL);
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

/* GET /api/prayers */
static esp_err_t get_prayers_handler(httpd_req_t *req)
{
    svc_prayer_engine_times_t times = {0};
    esp_err_t err = svc_prayer_engine_service_get_times(&times);
    
    cJSON *root = cJSON_CreateObject();
    if (err == ESP_OK && times.valid) {
        cJSON_AddBoolToObject(root, "valid", true);
        cJSON_AddNumberToObject(root, "fajr_sec", times.fajr_sec);
        cJSON_AddNumberToObject(root, "sunrise_sec", times.sunrise_sec);
        cJSON_AddNumberToObject(root, "dhuhr_sec", times.dhuhr_sec);
        cJSON_AddNumberToObject(root, "asr_sec", times.asr_sec);
        cJSON_AddNumberToObject(root, "maghrib_sec", times.maghrib_sec);
        cJSON_AddNumberToObject(root, "isha_sec", times.isha_sec);
    } else {
        cJSON_AddBoolToObject(root, "valid", false);
    }

    esp_err_t ret = send_json(req, root);
    cJSON_Delete(root);
    return ret;
}

esp_err_t svc_http_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t handlers[] = {
        { .uri = "/",             .method = HTTP_GET,  .handler = get_html_handler      },
        { .uri = "/api/config",   .method = HTTP_GET,  .handler = get_config_handler    },
        { .uri = "/api/config",   .method = HTTP_POST, .handler = post_config_handler   },
        { .uri = "/api/wifi",     .method = HTTP_POST, .handler = post_wifi_handler     },
        { .uri = "/api/reboot",        .method = HTTP_POST, .handler = post_reboot_handler        },
        { .uri = "/api/factory-reset", .method = HTTP_POST, .handler = post_factory_reset_handler },
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
