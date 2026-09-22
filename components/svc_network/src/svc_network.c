/*
 * svc_network.c
 *
 * ChronoLink V4 OS -- WiFi station service
 *
 * Responsibilities:
 *   - esp_netif + esp_wifi init, STA mode only
 *   - Read SSID/password from NVS namespace "wifi"
 *   - Connect, retry with exponential backoff on disconnect
 *   - Publish EVENT_NETWORK_* on the event bus
 *
 * NOT here: HTTP server, OTA, config-file I/O.
 *
 * Threading: wifi event handlers run in the wifi task context.
 * esp_wifi_connect() is called from WIFI_EVENT_STA_START directly
 * (non-blocking, standard Espressif pattern). On disconnect we do NOT
 * call esp_wifi_connect() from the handler -- we arm an esp_timer that
 * calls it from the esp_timer task.
 */
#include "svc_network.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "event_bus.h"
#include "network_events.h"

static const char *TAG = "svc_network";

#define NVS_NAMESPACE_WIFI   "wifi"
#define NVS_KEY_SSID         "ssid"
#define NVS_KEY_PASS         "pass"

#define WIFI_SSID_MAX_LEN    32
#define WIFI_PASS_MAX_LEN    64

#define SVC_NETWORK_BACKOFF_MIN_MS   1000
#define SVC_NETWORK_BACKOFF_MAX_MS  30000

/* ------------------------------------------------------------------ */
/* Internal state                                                     */
/* ------------------------------------------------------------------ */

static bool                         s_started      = false;
static bool                         s_connected    = false;
static esp_netif_t                 *s_sta_netif    = NULL;
static esp_event_handler_instance_t s_wifi_evt_instance = NULL;
static esp_event_handler_instance_t s_ip_evt_instance   = NULL;
static esp_timer_handle_t           s_reconnect_timer = NULL;
static uint32_t                     s_backoff_ms   = SVC_NETWORK_BACKOFF_MIN_MS;
static char                         s_ip_str[16]   = {0};

/* ------------------------------------------------------------------ */
/* Event publishing                                                   */
/* ------------------------------------------------------------------ */

/* Set type explicitly before copying payload. event_init_payload() in
   event_bus.h does not set evt.type when payload is NULL, so we do
   not rely on it for the NULL-payload case. */
static void publish_network_event(event_type_t type,
                                  const void *payload,
                                  size_t payload_len)
{
    event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = type;
    if (payload && payload_len > 0) {
        size_t n = payload_len > sizeof(evt.data) ? sizeof(evt.data) : payload_len;
        memcpy(evt.data, payload, n);
    }
    event_bus_publish(&evt);
}

static void publish_connected(const char *ip_str)
{
    network_event_payload_u p;
    memset(&p, 0, sizeof(p));
    if (ip_str) {
        strncpy(p.connected.ip_str, ip_str, sizeof(p.connected.ip_str) - 1);
    }
    publish_network_event(EVENT_NETWORK_CONNECTED, &p, sizeof(p));
}

static void publish_disconnected(uint8_t reason)
{
    network_event_payload_u p;
    memset(&p, 0, sizeof(p));
    p.disconnected.reason = reason;
    publish_network_event(EVENT_NETWORK_DISCONNECTED, &p, sizeof(p));
}

static void publish_error(svc_network_stage_t stage, int32_t err)
{
    network_event_payload_u p;
    memset(&p, 0, sizeof(p));
    p.error.stage = (uint8_t)stage;
    p.error.err   = err;
    publish_network_event(EVENT_NETWORK_ERROR, &p, sizeof(p));
}

/* ------------------------------------------------------------------ */
/* Credentials                                                        */
/* ------------------------------------------------------------------ */

static esp_err_t read_credentials(char *ssid_out, size_t ssid_cap,
                                  char *pass_out, size_t pass_cap)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace '%s' not present: %s",
                 NVS_NAMESPACE_WIFI, esp_err_to_name(err));
        return err;
    }

    size_t ssid_len = ssid_cap;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid_out, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS key '%s' missing: %s",
                 NVS_KEY_SSID, esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    size_t pass_len = pass_cap;
    err = nvs_get_str(h, NVS_KEY_PASS, pass_out, &pass_len);
    if (err != ESP_OK) {
        /* Password may legitimately be absent for open networks. */
        ESP_LOGI(TAG, "NVS key '%s' missing (open network?): %s",
                 NVS_KEY_PASS, esp_err_to_name(err));
        pass_out[0] = '\0';
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t svc_network_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid) > WIFI_SSID_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pass && strlen(pass) > WIFI_PASS_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(RW) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_PASS, pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Credentials stored (ssid='%s', pass=%s)",
                 ssid, pass && pass[0] ? "set" : "empty");
    } else {
        ESP_LOGE(TAG, "Failed to store credentials: %s", esp_err_to_name(err));
    }
    return err;
}

/* ------------------------------------------------------------------ */
/* Reconnect backoff                                                  */
/* ------------------------------------------------------------------ */

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Reconnect attempt (backoff was %lu ms)",
             (unsigned long)s_backoff_ms);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect() failed: %s", esp_err_to_name(err));
        publish_error(SVC_NETWORK_STAGE_RECONNECT, err);
    }
}

static void schedule_reconnect(void)
{
    if (!s_reconnect_timer) {
        return;
    }
    esp_timer_stop(s_reconnect_timer);   /* no-op if not running */
    esp_err_t err = esp_timer_start_once(s_reconnect_timer,
                                         (uint64_t)s_backoff_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_timer_start_once failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Reconnect scheduled in %lu ms",
             (unsigned long)s_backoff_ms);

    s_backoff_ms *= 2;
    if (s_backoff_ms > SVC_NETWORK_BACKOFF_MAX_MS) {
        s_backoff_ms = SVC_NETWORK_BACKOFF_MAX_MS;
    }
}

static void reset_backoff(void)
{
    s_backoff_ms = SVC_NETWORK_BACKOFF_MIN_MS;
    if (s_reconnect_timer) {
        esp_timer_stop(s_reconnect_timer);
    }
}

/* ------------------------------------------------------------------ */
/* Event handlers                                                     */
/* ------------------------------------------------------------------ */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "STA started, connecting...");
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Associated to AP, waiting for IP...");
        /* EVENT_NETWORK_CONNECTED fires on GOT_IP, not here. */
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *d =
            (const wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = d ? d->reason : 0;
        ESP_LOGW(TAG, "Disconnected (reason=%u)", (unsigned)reason);

        if (s_connected) {
            s_connected = false;
            s_ip_str[0] = '\0';
            publish_disconnected(reason);
        }
        schedule_reconnect();
        break;
    }

    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    if (event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&e->ip_info.ip));

        s_connected = true;
        reset_backoff();

        ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
        publish_connected(s_ip_str);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

esp_err_t svc_network_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    /* Credentials first; missing is non-fatal. */
    char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
    char pass[WIFI_PASS_MAX_LEN + 1] = {0};
    esp_err_t cred_err = read_credentials(ssid, sizeof(ssid),
                                          pass, sizeof(pass));
    if (cred_err != ESP_OK) {
        ESP_LOGW(TAG, "No WiFi credentials; service idle. "
                      "Call svc_network_set_credentials() and reboot.");
        publish_error(SVC_NETWORK_STAGE_NVS, cred_err);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Connecting to SSID '%s'", ssid);

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        publish_error(SVC_NETWORK_STAGE_INIT, err);
        return ESP_FAIL;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s",
                 esp_err_to_name(err));
        publish_error(SVC_NETWORK_STAGE_INIT, err);
        return ESP_FAIL;
    }

    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (!s_sta_netif) {
            ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
            publish_error(SVC_NETWORK_STAGE_INIT, ESP_FAIL);
            return ESP_FAIL;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        publish_error(SVC_NETWORK_STAGE_INIT, err);
        return ESP_FAIL;
    }

    /* Register handlers before esp_wifi_start so we don't miss
       WIFI_EVENT_STA_START. */
    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &wifi_event_handler,
                                              NULL,
                                              &s_wifi_evt_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register WIFI_EVENT failed: %s", esp_err_to_name(err));
        publish_error(SVC_NETWORK_STAGE_EVENT, err);
        esp_wifi_deinit();
        return ESP_FAIL;
    }

    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &ip_event_handler,
                                              NULL,
                                              &s_ip_evt_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register IP_EVENT failed: %s", esp_err_to_name(err));
        publish_error(SVC_NETWORK_STAGE_EVENT, err);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_evt_instance);
        s_wifi_evt_instance = NULL;
        esp_wifi_deinit();
        return ESP_FAIL;
    }

    if (!s_reconnect_timer) {
        const esp_timer_create_args_t targs = {
            .callback = &reconnect_timer_cb,
            .arg      = NULL,
            .name     = "svc_net_reconnect",
        };
        err = esp_timer_create(&targs, &s_reconnect_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
            publish_error(SVC_NETWORK_STAGE_INIT, err);
            esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  s_ip_evt_instance);
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  s_wifi_evt_instance);
            s_ip_evt_instance = NULL;
            s_wifi_evt_instance = NULL;
            esp_wifi_deinit();
            return ESP_FAIL;
        }
    }

    wifi_config_t wcfg;
    memset(&wcfg, 0, sizeof(wcfg));
    strncpy((char *)wcfg.sta.ssid,     ssid, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, pass, sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start sequence failed: %s", esp_err_to_name(err));
        publish_error(SVC_NETWORK_STAGE_CONNECT, err);
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "WiFi STA starting");
    publish_network_event(EVENT_NETWORK_STARTING, NULL, 0);
    return ESP_OK;
}

esp_err_t svc_network_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    if (s_reconnect_timer) {
        esp_timer_stop(s_reconnect_timer);
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    if (s_wifi_evt_instance) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_evt_instance);
        s_wifi_evt_instance = NULL;
    }
    if (s_ip_evt_instance) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              s_ip_evt_instance);
        s_ip_evt_instance = NULL;
    }

    s_connected = false;
    s_ip_str[0] = '\0';
    s_started = false;
    reset_backoff();

    ESP_LOGI(TAG, "WiFi stopped");
    return ESP_OK;
}

bool svc_network_is_connected(void)
{
    return s_connected;
}

const char *svc_network_get_ip_str(void)
{
    return s_connected ? s_ip_str : NULL;
}