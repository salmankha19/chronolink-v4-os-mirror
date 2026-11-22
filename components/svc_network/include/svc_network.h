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
#include <stddef.h>
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

/* Returns true while the provisioning AP is up. CR #4 unified the two
   scenarios it serves -- no credentials stored (setup) and a credential
   lockout (recovery) -- into a single identity, so a caller cannot and need
   not tell them apart: SSID "ChronoLink-<MAC4>", WPA2-PSK, passphrase
   regenerated on every entry. */
bool svc_network_is_ap_mode(void);

/* Dotted IPv4 string, or NULL if not connected. Owned by the service;
   valid until the next disconnect. Do not free. */
const char *svc_network_get_ip_str(void);

/* Store credentials in NVS. Does NOT reconnect; caller reboots or
   calls svc_network_stop() + _start() to apply.
   Returns ESP_ERR_INVALID_ARG if ssid is NULL/empty or ssid/pass
   exceed 802.11 limits (ssid <= 32, pass <= 64). */
esp_err_t svc_network_set_credentials(const char *ssid, const char *pass);

/*
 * Copy the current provisioning AP identity into out (NUL-terminated).
 * There is one AP identity for both entry paths (CR #4): setup and recovery
 * use the same SSID, so no caller has to know which one is up.
 *
 * SSID:       "ChronoLink-<MAC4>" -- the last two STA MAC bytes as %02X%02X
 *             (15 chars).
 * Passphrase: 16 uppercase hex characters from the hardware RNG, regenerated
 *             at every AP entry, RAM only, cleared by svc_network_stop().
 *
 * Returns ESP_ERR_INVALID_ARG   if out is NULL or cap is too small (SSID:
 *                               strlen(current SSID) + 1, passphrase: 17),
 *         ESP_ERR_INVALID_STATE if no provisioning AP is up, the identity has
 *                               not been generated yet, or (passphrase only)
 *                               the internal mutex cannot be taken.
 *
 * On any error out[] is left untouched. A caller that must not show a stale
 * identity treats ESP_ERR_INVALID_STATE as "not ready" and retries on its next
 * poll. The passphrase is a per-entry random draw: read it, do not cache it.
 */
esp_err_t svc_network_get_ap_ssid(char *out, size_t cap);
esp_err_t svc_network_get_ap_passphrase(char *out, size_t cap);

/*
 * httpd-side activity marker: extends the provisioning AP's idle timeout so a
 * setup session in progress is not cut off (design D7). Covers BOTH APs --
 * first-boot setup and the credential-recovery lockout -- because both now arm
 * the same 15 min idle / 30 min absolute one-shot.
 * Writes one timestamp; never stops or starts a timer.
 */
void svc_network_touch_provisioning_idle_timeout(void);

#ifdef __cplusplus
}
#endif