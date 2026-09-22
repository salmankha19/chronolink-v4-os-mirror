/*
 * svc_network.h
 *
 * ChronoLink V4 OS -- WiFi station service (public API)
 *
 * Scope: bring up the WiFi STA link, connect from credentials in NVS,
 * publish lifecycle events on the event bus.
 *
 * NOT in scope: HTTP server (svc_http), OTA (svc_ota), config-file
 * reading (svc_config). See docs/CHRONOLINK_CONSTITUTION.md section 9.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stage identifiers carried in network_error_payload_t.stage.
   Stable numeric values; do not reorder, only append. */
typedef enum {
    SVC_NETWORK_STAGE_INIT      = 0,  /* esp_netif / esp_wifi_init */
    SVC_NETWORK_STAGE_NVS       = 1,  /* reading credentials from NVS */
    SVC_NETWORK_STAGE_CONNECT   = 2,  /* esp_wifi_connect() */
    SVC_NETWORK_STAGE_EVENT     = 3,  /* event handler internal error */
    SVC_NETWORK_STAGE_RECONNECT = 4,  /* backoff timer path */
} svc_network_stage_t;

/*
 * Bring up the WiFi STA. Reads credentials from NVS namespace "wifi",
 * keys "ssid" / "pass" (both null-terminated strings).
 *
 * Non-blocking: returns after init is kicked off. Connection progress
 * is reported via event_bus (EVENT_NETWORK_*). Idempotent: a second
 * call after success is a no-op returning ESP_OK.
 *
 * Returns ESP_OK even if credentials are missing -- the OS is expected
 * to boot without WiFi. Missing credentials are reported via
 * EVENT_NETWORK_ERROR with stage == SVC_NETWORK_STAGE_NVS.
 *
 * Returns ESP_FAIL only on unrecoverable init failure (esp_netif or
 * esp_wifi_init failed); the service is then left disabled.
 */
esp_err_t svc_network_start(void);

/* Stop WiFi and release netif resources. Safe if not started. */
esp_err_t svc_network_stop(void);

/* Cached link state; does not block. */
bool svc_network_is_connected(void);

/* Dotted IPv4 string, or NULL if not connected. Owned by the service;
   valid until the next disconnect. Do not free. */
const char *svc_network_get_ip_str(void);

/* Store credentials in NVS. Does NOT reconnect; caller reboots or
   calls svc_network_stop() + _start() to apply.
   Returns ESP_ERR_INVALID_ARG if ssid is NULL/empty or ssid/pass
   exceed 802.11 limits (ssid <= 32, pass <= 64). */
esp_err_t svc_network_set_credentials(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif