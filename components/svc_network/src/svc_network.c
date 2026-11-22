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
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"      /* esp_read_mac() -- AP SSID suffix (CR #4) */
#include "esp_random.h"   /* esp_fill_random() -- AP passphrase (CR #4) */
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_system.h"   /* esp_get_free_heap_size() -- bring-up failure logs */
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

/* CR #2 -- WiFi lockout recovery.
   Threshold and windows are #defines, not Kconfig entries: same precedent
   as SVC_NETWORK_BACKOFF_MAX_MS above. See
   docs/sessions/cr2-wifi-lockout-design.md sections 6.2, 6.5, 6.8. */
#define SVC_NETWORK_AUTH_FAIL_THRESHOLD        5
/* CR #2 slow path (PR #157). Consecutive STA disconnects of ANY reason that
   raise the recovery AP. The auth threshold above only ever sees reasons
   2/15/202/204/205; a configured clock whose AP never becomes reachable --
   router off, 200/201 answers, a driver that keeps failing -- would otherwise
   retry STA forever with no way back in. #131 R1 covered only the "wait for
   the router" case, and the slow path keeps that: STA still retries first,
   just a bounded number of times. 24 is ~10 min at the capped 30 s backoff
   (1+2+4+8+16 s, then 30 s each), long enough to outlast a router or ISP
   modem that boots slower than the ESP32 after a power cut. */
#define SVC_NETWORK_UNREACHABLE_FAIL_THRESHOLD 24
#define SVC_NETWORK_RECOVERY_IDLE_WINDOW_US    (15LL * 60LL * 1000000LL)  /* 15 min */
#define SVC_NETWORK_RECOVERY_ABSOLUTE_CAP_US   (30LL * 60LL * 1000000LL)  /* 30 min */
#define SVC_NETWORK_COOLDOWN_US                (10LL * 60LL * 1000000LL)  /* 10 min */

/* The 15 min idle window and the 10 min cooldown are a matched pair:
   together they set the recovery AP's duty cycle (see
   docs/sessions/cr2-wifi-lockout-design.md). Do not change one without
   re-deriving the other -- an idle window alone determines how long an
   installer has, a cooldown alone determines how long they wait. */

/* Provisioning AP identity (CR #4, replacing the CR #2 recovery-only
   identity). SSID is "ChronoLink-" + the last two STA MAC bytes as %02X%02X
   (15 chars), so a 33-byte buffer covers the 802.11 maximum; 17 bytes cover
   the 16-char passphrase and its NUL. */
#define AP_SSID_BUF_LEN   33   /* 802.11 SSID max 32 + NUL */
#define AP_PASS_BYTES     8    /* 8 random bytes -> 16 hex chars */
#define AP_PASS_LEN       (2 * AP_PASS_BYTES)
#define AP_PASS_BUF_LEN   (AP_PASS_LEN + 1)

/* ------------------------------------------------------------------ */
/* Internal state                                                     */
/* ------------------------------------------------------------------ */

static bool                         s_started      = false;
static esp_netif_t                 *s_ap_netif     = NULL;
static bool                         s_connected    = false;
static bool                         s_ap_mode      = false;
static esp_netif_t                 *s_sta_netif    = NULL;
static esp_event_handler_instance_t s_wifi_evt_instance = NULL;
static esp_event_handler_instance_t s_ip_evt_instance   = NULL;
static esp_timer_handle_t           s_reconnect_timer = NULL;
static uint32_t                     s_backoff_ms   = SVC_NETWORK_BACKOFF_MIN_MS;
static char                         s_ip_str[16]   = {0};

/* --- CR #2 recovery state -------------------------------------------
 * s_recovery_active  : held ONLY across svc_network_stop() from a worker;
 *                      never across svc_network_start() (invariant 3).
 * s_recovery_mode    : true while the AP in front of us is the recovery AP
 *                      (the credential-lockout path). CR #4 gave both APs a
 *                      single identity and moved the httpd exemption to
 *                      s_ap_mode, so this flag now records WHICH AP is up;
 *                      the idle/absolute timeout is armed for both (see
 *                      s_setup_mode). Cleared first in stop(), set last in
 *                      start_ap_mode() (invariant 2).
 * s_setup_mode       : sibling of s_recovery_mode for AP_MODE_SETUP, set and
 *                      cleared the same way. The timeout callback is shared,
 *                      so the exit worker needs it to choose between "back to
 *                      STA" (recovery) and "cooldown, then re-enter the AP"
 *                      (setup).
 * s_force_recovery   : makes the next svc_network_start() take the
 *                      recovery-AP branch instead of STA.
 * s_cooldown_active  : 10 min after an AP exit; suppresses the failure
 *                      counters but not the STA retry.
 * s_setup_cooldown_active : true between a setup AP's timeout and its
 *                      re-entry timer. Freezes both failure counters and is
 *                      what stops a stray svc_network_start() from bouncing
 *                      the setup AP straight back up.
 * s_generation       : bumped by svc_network_stop(); a worker compares its
 *                      snapshot to detect a restart it must not overrule.
 * s_consecutive_fail : consecutive STA disconnects of ANY reason, counted
 *                      under the same guard as the auth streak. Reaching
 *                      SVC_NETWORK_UNREACHABLE_FAIL_THRESHOLD (24) enters
 *                      recovery even when the reason is not auth-class.
 * s_ap_identity_lock : static mutex guarding the AP identity strings
 *                      (SSID + passphrase); NULL means uninitialised, which
 *                      makes the getters fail closed.
 * -------------------------------------------------------------------- */
static bool                             s_recovery_active        = false;
static bool                             s_recovery_mode          = false;
static bool                             s_setup_mode             = false;
static bool                             s_force_recovery         = false;
static bool                             s_cooldown_active        = false;
static bool                             s_setup_cooldown_active  = false;
static uint8_t                          s_consecutive_auth_fail  = 0;
static uint8_t                          s_consecutive_fail       = 0;
static uint32_t                         s_generation             = 0;
static int64_t                          s_recovery_entry_us         = 0;
static int64_t                          s_last_recovery_activity_us = 0;
static uint32_t                         s_bringup_backoff_ms     = SVC_NETWORK_BACKOFF_MIN_MS;
static char                             s_ap_ssid[AP_SSID_BUF_LEN] = {0};
static char                             s_ap_pass[AP_PASS_BUF_LEN] = {0};
static StaticSemaphore_t                s_ap_identity_lock_storage;
static SemaphoreHandle_t                s_ap_identity_lock      = NULL;
static esp_timer_handle_t               s_recovery_entry_trigger_timer = NULL;
static esp_timer_handle_t               s_recovery_timeout_timer       = NULL;
static esp_timer_handle_t               s_bringup_retry_timer          = NULL;
static esp_timer_handle_t               s_cooldown_timer               = NULL;
static esp_timer_handle_t               s_setup_reentry_timer          = NULL;

/* ------------------------------------------------------------------ */
/* Recovery helpers (CR #2)                                           */
/* ------------------------------------------------------------------ */

/* Design section 6.1 -- pure predicate, no state. Whitelist frozen at
   {2, 15, 202, 204, 205} (values verified against esp_wifi_types_generic.h,
   IDF v6.0). 205 CONNECTION_FAIL is counted as auth-class: the AP was seen
   and the association/handshake was rejected, which is what a wrong password
   looks like on routers that answer 205 instead of 15/202/204. Leaving it out
   reset the streak, so a run that mixed 15/202 with 205 never reached the
   threshold and a pure-205 wrong password only ever hit the slow path.
   Deliberately NOT counted: 200 BEACON_TIMEOUT and 201 NO_AP_FOUND, which mean
   the router is down or out of range; #131 R1 requires that case to stay on
   plain STA retry forever, with the 24-strike slow path as its bounded
   backstop. Deliberately deferred: 210 / 211 (WPA3-only router). */
static bool reason_is_auth_class(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:              /* 202 */
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: /* 15  */
    case WIFI_REASON_CONNECTION_FAIL:        /* 205 */
    case WIFI_REASON_HANDSHAKE_TIMEOUT:      /* 204 */
    case WIFI_REASON_AUTH_EXPIRE:            /* 2   */
        return true;
    default:
        return false;
    }
}

/* Design section 6.2.1 -- the canonical state guard. One expression; the
 * disconnect handler tests this immediately before incrementing the
 * counter. The reconnect decision is a SEPARATE test: a non-auth reason
 * must still call schedule_reconnect().
 * Only s_recovery_active also suppresses schedule_reconnect();
 * the mode flags and the two cooldowns suppress the counters but not the
 * STA retry (during an AP the STA driver is not running anyway, and during
 * a cooldown STA is retrying normally).
 * s_setup_mode / s_setup_cooldown_active came with setup-AP timeout parity:
 * a disconnect can then only arrive from a race with the teardown, and during
 * the setup cooldown the link is deliberately down -- neither may advance the
 * 24-strike slow path. */
static bool recovery_guards_active(void)
{
    return s_recovery_active || s_recovery_mode || s_setup_mode ||
           s_cooldown_active || s_setup_cooldown_active;
}

/* CR #4 -- 16 uppercase hex chars, regenerated at every AP entry.
 * Ephemeral: RAM only, never written to NVS, cleared by svc_network_stop().
 * The hex loop mirrors load_or_generate_api_token() in svc_http.c, which is
 * deliberately NOT refactored by this change.
 * WPA2-PSK requires 8..63 characters; 16 always satisfies that.
 *
 * Lock discipline (design section 7, C5): this helper takes
 * s_ap_identity_lock itself, so callers must NOT hold it across the call.
 * With no mutex it fails closed and leaves the identity untouched -- neither
 * string is ever written unlocked. */
static esp_err_t generate_ap_passphrase(void)
{
    if (!s_ap_identity_lock) {
        ESP_LOGE(TAG, "AP identity mutex not initialised; no passphrase");
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_ap_identity_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "AP identity mutex take failed (passphrase)");
        return ESP_ERR_TIMEOUT;
    }

    uint8_t raw[AP_PASS_BYTES];
    esp_fill_random(raw, sizeof(raw));   /* hardware RNG, 8 bytes */
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < sizeof(raw); i++) {
        s_ap_pass[2 * i]     = hex[raw[i] >> 4];
        s_ap_pass[2 * i + 1] = hex[raw[i] & 0x0F];
    }
    s_ap_pass[AP_PASS_LEN] = '\0';   /* index 16 */

    xSemaphoreGive(s_ap_identity_lock);
    return ESP_OK;
}

/* CR #4 -- build the single provisioning-AP identity: SSID
 * "ChronoLink-<MAC4>" plus a fresh passphrase, for BOTH entry paths (no
 * credentials at boot and credential lockout), so nothing over the air
 * distinguishes them.
 *
 * Both strings are written inside one critical section, so a getter can never
 * observe a torn identity (a new SSID with the previous passphrase). On
 * failure nothing is partially written: the SSID is only formatted after the
 * MAC read succeeds, and a failed mutex take returns before either write. */
static esp_err_t generate_ap_identity(void)
{
    if (!s_ap_identity_lock) {
        ESP_LOGE(TAG, "AP identity mutex not initialised; no identity");
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_ap_identity_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "AP identity mutex take failed (identity)");
        return ESP_ERR_TIMEOUT;
    }

    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_read_mac(ESP_MAC_WIFI_STA) failed: %s",
                 esp_err_to_name(err));
        xSemaphoreGive(s_ap_identity_lock);
        return err;
    }

    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ChronoLink-%02X%02X",
             mac[4], mac[5]);

    xSemaphoreGive(s_ap_identity_lock);

    return generate_ap_passphrase();
}

/* ------------------------------------------------------------------ */
/* Event publishing                                                   */
/* ------------------------------------------------------------------ */

/* Set type explicitly before copying payload. event_init_payload() in
   event_bus.h now sets evt.type even for a NULL/empty payload (CR S5),
   but this helper keeps the explicit memset + assignment so the
   NULL-payload case is readable without cross-referencing that header. */
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

/* D8: create the AP identity mutex on first use. Static storage means this
 * cannot fragment the heap, and xSemaphoreCreateMutexStatic() does not
 * allocate. Idempotent, so every svc_network_start() may call it. Not
 * itself locked: the callers (boot path, entry/exit/retry workers) are
 * serialised by s_started / s_recovery_active. */
static bool ensure_ap_identity_lock(void)
{
    if (s_ap_identity_lock) {
        return true;
    }
    s_ap_identity_lock = xSemaphoreCreateMutexStatic(&s_ap_identity_lock_storage);
    return s_ap_identity_lock != NULL;
}

/* ------------------------------------------------------------------ */
/* CR #2 -- recovery workers and timer callbacks                      */
/* ------------------------------------------------------------------ */

/* PR #157 -- the single guarded entry into the recovery AP. Both triggers
 * use it: the auth-class streak (SVC_NETWORK_AUTH_FAIL_THRESHOLD) and the
 * unreachable slow path (SVC_NETWORK_UNREACHABLE_FAIL_THRESHOLD). Returns
 * true when the entry is armed; the caller must then NOT schedule a
 * reconnect, because the entry worker's stop()/start() owns the link.
 *
 * Returns false after rolling BOTH flags back, so the caller falls through
 * to the normal STA ladder. Leaving either flag set on failure would make
 * recovery_guards_active() true forever: the counters stop counting and the
 * device silently loses its only path back to the network.
 *
 * s_recovery_active is set BEFORE arming: it gates a double spawn, and the
 * guard it raises makes any disconnect that arrives inside the 250 ms window
 * a no-op. */
static bool arm_recovery_entry(void)
{
    s_force_recovery  = true;
    s_recovery_active = true;   /* set BEFORE arming: gates double-spawn */
    /* Arm a 250 ms one-shot; the callback spawns the worker. Never
       xTaskCreate here: that allocates on the Wi-Fi event task,
       and this file already arms timers for that reason. */
    esp_err_t arm_err = s_recovery_entry_trigger_timer
        ? esp_timer_start_once(s_recovery_entry_trigger_timer,
                               250ULL * 1000ULL)
        : ESP_ERR_INVALID_STATE;
    if (arm_err != ESP_OK) {
        ESP_LOGE(TAG, "failed to arm recovery entry (%s)",
                 esp_err_to_name(arm_err));
        s_force_recovery  = false;
        s_recovery_active = false;
        return false;
    }
    return true;
}

/* Design section 6.7 (E1): the bring-up retry ladder. Deliberately separate
 * from the STA ladder (s_backoff_ms): svc_network_stop() resets that one on
 * every cycle, so a shared ladder would retry esp_wifi_init() once per second
 * forever on exactly the device this retry exists to rescue. The value is
 * doubled only after a successful arm, so the delay that was used is the one
 * that was logged. */
static void arm_bringup_retry(void)
{
    if (!s_bringup_retry_timer) {
        ESP_LOGE(TAG, "bring-up retry timer missing; cannot arm");
        return;
    }
    /* Restartable by design: a pending retry is pushed back, never pulled
     * forward, so no caller can shortcut the ladder. */
    esp_timer_stop(s_bringup_retry_timer);   /* no-op if not running */
    esp_err_t err = esp_timer_start_once(s_bringup_retry_timer,
                                         (uint64_t)s_bringup_backoff_ms * 1000ULL);
    if (err != ESP_OK) {
        /* No flag is cleared here: the one-shot is not armed, so the next
         * arm_bringup_retry() call *is* the retry. */
        ESP_LOGE(TAG, "failed to arm bring-up retry: %s", esp_err_to_name(err));
        return;
    }
    s_bringup_backoff_ms *= 2;
    if (s_bringup_backoff_ms > SVC_NETWORK_BACKOFF_MAX_MS) {
        s_bringup_backoff_ms = SVC_NETWORK_BACKOFF_MAX_MS;
    }
}

/* Last resort for a failed bring-up (design sections 6.3, 6.6, 6.7): walk the
 * bring-up ladder until esp_wifi_init() (or whatever failed) succeeds. Same
 * philosophy as #131 R1 -- self-heal, do not require a power cycle. */
static void bringup_retry_worker(void *arg)
{
    (void)arg;

    svc_network_stop();          /* C1: every start() in a worker is preceded by stop() */
    s_recovery_active = false;   /* invariant 3: after stop() returns, before start() */
    esp_err_t err = svc_network_start();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bring-up retry failed (%s, free heap=%u)",
                 esp_err_to_name(err), (unsigned)esp_get_free_heap_size());
        publish_error(SVC_NETWORK_STAGE_CONNECT, err);
        arm_bringup_retry();
    } else {
        s_bringup_backoff_ms = SVC_NETWORK_BACKOFF_MIN_MS;   /* E1 */
    }
    vTaskDelete(NULL);
}

static void bringup_retry_cb(void *arg)
{
    (void)arg;
    if (xTaskCreatePinnedToCore(bringup_retry_worker, "svc_net_retry",
                                4096, NULL, tskIDLE_PRIORITY + 5, NULL, 0)
        != pdPASS) {
        ESP_LOGE(TAG, "bring-up retry spawn failed, rearming");
        /* The worker never ran, so nothing advanced the ladder: re-arming
         * here advances it, which keeps a failed spawn off a 1 s hot loop. */
        arm_bringup_retry();
    }
}

/* Setup-AP timeout parity -- arm the one-shot that ends the setup cooldown.
 * The exit worker calls this after a setup AP times out instead of calling
 * svc_network_start(): with no credentials that start() would take the
 * empty-SSID branch and raise the AP again on the spot, turning every exit
 * into a 1 s entry/exit loop.
 *
 * Deliberately a timer and not "let STA fail until the 24-strike slow path":
 * an empty SSID cannot associate, so the only STA outcomes are a config
 * rejection or a NO_AP_FOUND storm, and neither is a predictable countdown.
 * The delay is SVC_NETWORK_COOLDOWN_US, the same 10 min the exit cooldown
 * uses, so the two never disagree.
 *
 * Never pulls a pending re-entry forward: if the one-shot is already running
 * it owns the next transition and this call is a no-op, so a stray start()
 * cannot postpone the re-entry forever. */
static bool arm_setup_reentry(uint64_t delay_us)
{
    if (!s_setup_reentry_timer) {
        ESP_LOGE(TAG, "setup re-entry timer missing");
        return false;
    }
    if (esp_timer_is_active(s_setup_reentry_timer)) {
        return true;
    }
    esp_err_t err = esp_timer_start_once(s_setup_reentry_timer, delay_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to arm setup re-entry: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

/* Runs when the setup cooldown expires. stop() first for the same reason every
 * other worker does it: it is normally a no-op here (the exit worker already
 * stopped everything), but a sticky s_started would make the start() below a
 * silent no-op instead of a fresh bring-up. */
static void setup_reentry_worker(void *arg)
{
    (void)arg;

    s_setup_cooldown_active = false;   /* released before start() re-enters */
    svc_network_stop();
    s_recovery_active = false;         /* invariant 3 */

    esp_err_t err = svc_network_start();   /* empty SSID -> AP_MODE_SETUP */

    /* Only a non-ESP_OK return is a failure. ESP_OK with s_ap_mode false is not:
       credentials may have been written during the AP session (the installer
       using /api/wifi), in which case start() correctly takes the STA path and
       the empty-SSID branch is never reached. Testing !s_ap_mode here would arm
       the bring-up ladder and turn that case into a stop/start loop. */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "setup AP re-entry failed (%s, free heap=%u)",
                 esp_err_to_name(err), (unsigned)esp_get_free_heap_size());
        publish_error(SVC_NETWORK_STAGE_CONNECT, err);
        arm_bringup_retry();
    } else if (s_ap_mode) {
        s_bringup_backoff_ms = SVC_NETWORK_BACKOFF_MIN_MS;   /* E1 */
    } else {
        ESP_LOGI(TAG, "setup re-entry: credentials now present, STA path taken");
    }
    vTaskDelete(NULL);
}

static void setup_reentry_cb(void *arg)
{
    (void)arg;
    if (xTaskCreatePinnedToCore(setup_reentry_worker, "svc_net_ap_re",
                                4096, NULL, tskIDLE_PRIORITY + 5, NULL, 0)
        != pdPASS) {
        /* The one-shot already fired, so nothing is pending. Arm a short retry
         * instead of stranding the device with no link and no AP. */
        ESP_LOGE(TAG, "setup re-entry worker spawn failed, retrying in 30 s");
        if (!arm_setup_reentry(30LL * 1000000LL)) {
            ESP_LOGE(TAG, "setup re-entry lost; next reboot re-enters the AP");
        }
    }
}

static void cooldown_cb(void *arg)
{
    (void)arg;
    s_cooldown_active = false;
}

/* Design section 6.5 -- seed the entry/activity timestamps and arm the single
 * one-shot that enforces both bounds (the absolute cap needs no second timer:
 * recovery_timeout_cb() re-arms for the shorter of the two remainders).
 *
 * Shared since setup-AP timeout parity: the recovery entry worker calls it once
 * its AP is up, start_ap_mode() calls it for AP_MODE_SETUP. One timer, one
 * decision callback and one set of bounds for both APs is what keeps the two
 * paths identical.
 *
 * The error is returned rather than handled here: the callers have different
 * safe fallbacks (roll back to STA vs. keep the AP up). */
static esp_err_t arm_ap_timeout(void)
{
    if (!s_recovery_timeout_timer) {
        return ESP_ERR_INVALID_STATE;
    }
    int64_t now                 = esp_timer_get_time();
    s_recovery_entry_us         = now;
    s_last_recovery_activity_us = now;
    return esp_timer_start_once(s_recovery_timeout_timer,
                                SVC_NETWORK_RECOVERY_IDLE_WINDOW_US);
}

/* Design section 6.3 -- recovery entry worker. Spawned from
 * s_recovery_entry_trigger_timer (never from the Wi-Fi event task: an
 * xTaskCreate on a months-uptime fragmented heap is avoidable, and this file
 * already arms timers for that reason). */
static void recovery_entry_worker(void *arg)
{
    (void)arg;

    svc_network_stop();          /* invariant 3: s_recovery_active is held only across teardown */
    s_recovery_active = false;   /* C1/invariant 3: cleared after stop() returns, before start() */

    esp_err_t err = svc_network_start();   /* recovery branch: s_force_recovery is set */

    /* A plain bool read suffices. This worker is the task that just ran
     * start_ap_mode(), and s_recovery_mode is set there under
     * s_ap_identity_lock only after esp_wifi_start() returned ESP_OK. */
    if (err != ESP_OK || !s_recovery_mode) {
        /* C1: fall back to the STA path. stop() is mandatory first because
         * s_started is sticky on partial failure -- start() would no-op. */
        s_force_recovery = false;
        svc_network_stop();
        s_recovery_active = false;
        err = svc_network_start();      /* STA path */

        if (err != ESP_OK) {
            /* C1 terminal-state replacement: do not give up, walk the ladder. */
            ESP_LOGE(TAG, "bring-up failed after recovery entry fallback "
                          "(err=%s, free heap=%u)",
                     esp_err_to_name(err), (unsigned)esp_get_free_heap_size());
            publish_error(SVC_NETWORK_STAGE_CONNECT, err);
            arm_bringup_retry();
        } else {
            s_bringup_backoff_ms = SVC_NETWORK_BACKOFF_MIN_MS;   /* E1 */
        }
        s_force_recovery = false;
        vTaskDelete(NULL);
        return;
    }

    /* Recovery AP is up. Arm the idle + absolute timeout (design section 6.5);
       the setup AP arms the same one-shot through the same helper. */
    uint32_t gen = s_generation;   /* D6: snapshot before the arm */
    err = arm_ap_timeout();
    if (err != ESP_OK) {
        /* No timer = no exit = permanent stranding. Roll back to STA. */
        ESP_LOGE(TAG, "failed to arm recovery timeout (%s), rolling back",
                 esp_err_to_name(err));
        s_force_recovery = false;
        svc_network_stop();
        s_recovery_active = false;
        (void)svc_network_start();
        if (gen != s_generation) {   /* D6: another teardown raced us */
            vTaskDelete(NULL);
            return;
        }
    }
    s_bringup_backoff_ms = SVC_NETWORK_BACKOFF_MIN_MS;   /* E1: the AP came up */
    s_force_recovery     = false;
    vTaskDelete(NULL);
}

/* Design section 6.6 -- exit worker. Runs when the idle window or the absolute
 * cap expires (for either AP), and is the only writer of the cooldowns (D5).
 * A recovery exit returns to STA with the possibly corrected credentials; a
 * setup exit has no credentials to return to, so it holds the link down for
 * SVC_NETWORK_COOLDOWN_US and lets the re-entry timer raise the AP again
 * (setup-AP timeout parity). */
static void recovery_exit_worker(void *arg)
{
    (void)arg;

    /* Snapshot the mode BEFORE stop(): stop() clears both flags first, under
       the identity mutex (invariant 2). A plain bool read is enough here -- the
       AP is up, so s_started is true and no worker can be inside
       start_ap_mode() rewriting it. */
    bool setup_ap = s_setup_mode;

    svc_network_stop();          /* clears s_recovery_mode / s_setup_mode FIRST, under the mutex */
    s_recovery_active = false;   /* invariant 3: after stop() returns, before start() */

    /* start() below must take the STA path. Design section 6.6 assumes the
     * flag is already false (the entry worker clears it); clearing it here as
     * well makes that true by construction -- if a latent bug left it set,
     * this exit would silently re-enter recovery. */
    s_force_recovery = false;

    /* Setup AP: do NOT call start(). The empty-SSID branch would raise the
     * setup AP on the spot, so every exit would be a 1 s entry/exit loop. The
     * re-entry timer owns the next transition instead. Nothing is started in
     * the meantime: STA cannot associate without an SSID, so the link is
     * deliberately down rather than spinning (and s_setup_cooldown_active
     * freezes both failure counters while it is). */
    if (setup_ap) {
        s_setup_cooldown_active = true;
        if (!arm_setup_reentry(SVC_NETWORK_COOLDOWN_US)) {
            /* No timer = no re-entry = no AP until the next reboot. Fail open:
               put the setup AP back now and accept the entry/exit cycle. */
            s_setup_cooldown_active = false;
            (void)svc_network_start();
        }
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = svc_network_start();   /* STA path */

    /* D5: the cooldown is armed regardless of the STA outcome -- a failed
     * restart must not immediately re-trigger recovery. Flag first, then the
     * timer; clear the flag again if the arm fails, so the predicate never
     * reports a cooldown that is not running. */
    s_cooldown_active = true;
    if (!s_cooldown_timer ||
        esp_timer_start_once(s_cooldown_timer, SVC_NETWORK_COOLDOWN_US) != ESP_OK) {
        ESP_LOGE(TAG, "failed to arm cooldown; clearing flag");
        s_cooldown_active = false;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "post-recovery STA start failed (%s), arming bring-up retry",
                 esp_err_to_name(err));
        publish_error(SVC_NETWORK_STAGE_CONNECT, err);
        arm_bringup_retry();
    } else {
        s_bringup_backoff_ms    = SVC_NETWORK_BACKOFF_MIN_MS;   /* E1 */
        s_consecutive_auth_fail = 0;                            /* F4 */
        s_consecutive_fail      = 0;                            /* PR #157 */
    }
    vTaskDelete(NULL);
}

/* Design section 6.5 -- single writer, single decision point (D7 + E3). The
 * activity timestamp is written by
 * svc_network_touch_provisioning_idle_timeout() only; this callback never
 * stops a timer another task may re-arm. Serves both APs: the timestamps it
 * reads are seeded by arm_ap_timeout() at every entry. */
static void recovery_timeout_cb(void *arg)
{
    (void)arg;
    int64_t now      = esp_timer_get_time();
    int64_t idle     = now - s_last_recovery_activity_us;
    int64_t lifetime = now - s_recovery_entry_us;

    if (lifetime >= SVC_NETWORK_RECOVERY_ABSOLUTE_CAP_US ||
        idle     >= SVC_NETWORK_RECOVERY_IDLE_WINDOW_US) {
        /* Exit. Flag first, so the exit worker's teardown cannot be counted
         * as a fresh auth-class run. */
        s_recovery_active = true;
        if (xTaskCreatePinnedToCore(recovery_exit_worker, "svc_net_rec_out",
                                    4096, NULL, tskIDLE_PRIORITY + 5, NULL, 0)
            != pdPASS) {
            /* D10: the one-shot already fired, so nothing is pending -- re-arm
             * and retry instead of stranding the device in recovery. */
            ESP_LOGE(TAG, "exit worker spawn failed, rearming in 30 s");
            s_recovery_active = false;
            if (s_recovery_timeout_timer) {
                (void)esp_timer_start_once(s_recovery_timeout_timer,
                                           30LL * 1000000LL);
            }
        }
    } else {
        /* Re-arm for the shorter of the two remainders. */
        int64_t next = SVC_NETWORK_RECOVERY_IDLE_WINDOW_US - idle;
        int64_t cap  = SVC_NETWORK_RECOVERY_ABSOLUTE_CAP_US - lifetime;
        if (cap < next) {
            next = cap;
        }
        if (s_recovery_timeout_timer) {
            (void)esp_timer_start_once(s_recovery_timeout_timer, (uint64_t)next);
        }
    }
}

/* Design section 6.3.1 -- a failed spawn here leaves the device in plain STA
 * retry, which is safe: unlike the timeout callback, nothing is stranded. */
static void recovery_entry_trigger_cb(void *arg)
{
    (void)arg;
    if (xTaskCreatePinnedToCore(recovery_entry_worker, "svc_net_rec_in",
                                4096, NULL, tskIDLE_PRIORITY + 5, NULL, 0)
        != pdPASS) {
        ESP_LOGE(TAG, "recovery_entry_worker spawn failed");
        s_force_recovery  = false;
        s_recovery_active = false;
        schedule_reconnect();
    }
}

/* D8 sibling of ensure_recovery_lock(): create the five AP timers once.
 * esp_timer_create() allocates from the heap, so this runs from
 * svc_network_start() -- never from a Wi-Fi event handler or a timer callback.
 * Idempotent and partial-safe; the timers are never deleted, only stopped in
 * svc_network_stop() (except the cooldown and the setup re-entry, which must
 * outlive a stop()), so a stop()/start() cycle costs no allocation.
 * esp_timer names are capped at 15 characters (ESP_TIMER_MAX_NAME_LEN - 1). */
static bool ensure_recovery_timers(void)
{
    if (!s_recovery_entry_trigger_timer) {
        const esp_timer_create_args_t a = {
            .callback = &recovery_entry_trigger_cb,
            .arg      = NULL,
            .name     = "rec_entry",
        };
        if (esp_timer_create(&a, &s_recovery_entry_trigger_timer) != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create(rec_entry) failed");
            return false;
        }
    }
    if (!s_recovery_timeout_timer) {
        const esp_timer_create_args_t a = {
            .callback = &recovery_timeout_cb,
            .arg      = NULL,
            .name     = "rec_timeout",
        };
        if (esp_timer_create(&a, &s_recovery_timeout_timer) != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create(rec_timeout) failed");
            return false;
        }
    }
    if (!s_bringup_retry_timer) {
        const esp_timer_create_args_t a = {
            .callback = &bringup_retry_cb,
            .arg      = NULL,
            .name     = "rec_bu_retry",
        };
        if (esp_timer_create(&a, &s_bringup_retry_timer) != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create(rec_bu_retry) failed");
            return false;
        }
    }
    if (!s_cooldown_timer) {
        const esp_timer_create_args_t a = {
            .callback = &cooldown_cb,
            .arg      = NULL,
            .name     = "rec_cooldown",
        };
        if (esp_timer_create(&a, &s_cooldown_timer) != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create(rec_cooldown) failed");
            return false;
        }
    }
    if (!s_setup_reentry_timer) {
        const esp_timer_create_args_t a = {
            .callback = &setup_reentry_cb,
            .arg      = NULL,
            .name     = "ap_reentry",
        };
        if (esp_timer_create(&a, &s_setup_reentry_timer) != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create(ap_reentry) failed");
            return false;
        }
    }
    return true;
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
        int8_t  rssi   = d ? d->rssi : 0;
        /* rssi is logged on every disconnect, not only on qualifying ones:
           the negative cases are what prove #131 R1 is untouched. */
        ESP_LOGW(TAG, "Disconnected (reason=%u rssi=%d)",
                 (unsigned)reason, (int)rssi);

        if (s_connected) {
            s_connected = false;
            s_ip_str[0] = '\0';
            publish_disconnected(reason);
        }

        bool auth_class = reason_is_auth_class(reason);

        /* CR #2 (design section 6.2.2): count only while no guard is active.
           recovery_guards_active() guards BOTH counters; s_recovery_active
           alone also guards schedule_reconnect(). */
        if (!recovery_guards_active()) {
            /* Clamp: a counter only has to reach its threshold, and a wrap
               to 0 would restart a fresh run from inside an already-stale
               one. */
            if (s_consecutive_fail < 0xFF) {
                s_consecutive_fail++;
            }
            if (auth_class) {
                if (s_consecutive_auth_fail < 0xFF) {
                    s_consecutive_auth_fail++;
                }
                ESP_LOGW(TAG, "auth-class reason=%u, consecutive=%u",
                         (unsigned)reason, (unsigned)s_consecutive_auth_fail);
                if (s_consecutive_auth_fail >= SVC_NETWORK_AUTH_FAIL_THRESHOLD) {
                    if (arm_recovery_entry()) {
                        return;   /* the entry worker owns the link now */
                    }
                    /* Arm failed: both flags are rolled back, so fall through
                       to the ordinary ladder below. */
                }
            } else {
                /* F4: only an unbroken series of auth-class failures is
                   evidence of wrong credentials; anything else ends the run. */
                s_consecutive_auth_fail = 0;
            }

            /* CR #2 slow path (PR #157): a configured clock whose AP never
               becomes reachable must still become reachable itself. The
               auth-class ladder above only ever sees 2/15/202/204/205, so a
               router that is off, an AP answering 200/201, or a driver that
               keeps failing would otherwise retry STA forever (#131 R1
               assumed "the router will come up"; nothing guaranteed it). 24
               consecutive failures is ~10 min at the capped 30 s backoff. */
            if (s_consecutive_fail >= SVC_NETWORK_UNREACHABLE_FAIL_THRESHOLD) {
                ESP_LOGW(TAG, "AP unreachable (%u consecutive failures), "
                              "entering recovery",
                         (unsigned)s_consecutive_fail);
                if (arm_recovery_entry()) {
                    return;   /* the entry worker owns the link now */
                }
            }
        } else if (!auth_class) {
            /* F4 in the guarded case: a non-auth disconnect ends the auth
               run even while a guard is active, so a stale streak cannot
               survive a cooldown or a recovery cycle. */
            s_consecutive_auth_fail = 0;
        }

        /* #131 (R1): a plain STA retry, never AP, unless a trigger above
           already returned. Boot before the router is up is a normal
           power-cut scenario; calling start_ap_mode() from here would lock
           the device in setup mode until the next power-cycle. The bounded
           slow path above is what makes recovery reachable, not this branch.
           s_recovery_active still suppresses the retry on its own: during
           teardown the STA driver is going away, and arming a reconnect into
           it would race svc_network_stop(). */
        if (!s_recovery_active) {
            schedule_reconnect();
        }
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

        /* F4: a working link ends any auth-fail run, and the slow path's
           failure run with it (PR #157) -- otherwise a link that just came
           back would still be one disconnect away from the trigger. */
        s_consecutive_auth_fail = 0;
        s_consecutive_fail      = 0;

        /* E1 defence in depth: svc_network_start() already reset the ladder on
           its ESP_OK return; repeating it here keeps the invariant true for
           any path that reaches GOT_IP. */
        s_bringup_backoff_ms = SVC_NETWORK_BACKOFF_MIN_MS;

        /* Design section 6.8: the cooldown is cleared ONLY on GOT_IP, never on
           WIFI_EVENT_STA_CONNECTED -- on WPA2-PSK association succeeds before
           the 4-way handshake fails, so clearing on STA_CONNECTED would clear
           the cooldown on the first retry with still-wrong credentials (C2). */
        s_cooldown_active = false;
        if (s_cooldown_timer) {
            esp_timer_stop(s_cooldown_timer);
        }

        /* A successful link cancels a pending recovery entry, but only if the
           entry worker has not already started: once s_recovery_active is set
           the teardown is in flight and cancelling here would strand it. */
        if (s_force_recovery && !s_recovery_active) {
            s_force_recovery = false;
            if (s_recovery_entry_trigger_timer) {
                esp_timer_stop(s_recovery_entry_trigger_timer);
            }
        }

        ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
        publish_connected(s_ip_str);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

/* CR #2: which AP the parameterised start_ap_mode() brings up. CR #4 gives
   both entries the same SSID and the same WPA2 treatment; since setup-AP
   timeout parity the value also selects only the exit behaviour
   (recovery_exit_worker(): back to STA vs. cooldown + re-entry), because both
   paths arm the same idle/absolute timeout via arm_ap_timeout(). */
typedef enum {
    AP_MODE_SETUP    = 0,  /* no credentials at boot: ChronoLink-<MAC4>,
                              WPA2-PSK, passphrase shown on the display      */
    AP_MODE_RECOVERY = 1,  /* auth-class / unreachable lockout:
                              ChronoLink-<MAC4>, WPA2-PSK, passphrase shown  */
} ap_mode_t;

static void start_ap_mode(ap_mode_t mode)
{
    if (s_ap_mode) return;

    const bool recovery = (mode == AP_MODE_RECOVERY);

    /* CR #4: one identity for both entry paths, generated HERE so it rotates
       on every AP entry (the recovery branch used to prepare it beforehand;
       doing it in one place keeps the two paths identical).

       Fail closed: without a passphrase there is no physical-possession proof,
       so never fall back to an open AP. Returning here leaves s_ap_mode false;
       svc_network_start() then arms the bring-up retry ladder instead of
       pretending the AP is up. */
    esp_err_t gen_err = generate_ap_identity();
    if (gen_err != ESP_OK) {
        ESP_LOGE(TAG, "AP identity generation failed: %s",
                 esp_err_to_name(gen_err));
        return;
    }

    ESP_LOGW(TAG, "Starting provisioning AP (SSID: %s)", s_ap_ssid);

    /* Bench aid: a board with no panel cannot show the passphrase that
       ui_producer would draw, so the AP is unjoinable on the bench without
       this. Same build-flag pattern as the API token print in svc_http.c: opt
       in with -DCHRONOLINK_DEBUG_PRINT_TOKEN=1 in platformio.local.ini
       (git-ignored). With the flag undefined the line and its format string
       are removed by the preprocessor, so nothing leaks in a shipped image.
       Safe to print here: generate_ap_identity() returned ESP_OK, so the
       passphrase is a full 16 chars, and this task is the only writer. */
#if defined(CHRONOLINK_DEBUG_PRINT_TOKEN)
    ESP_LOGW(TAG, " AP passphrase (bench debug): %s", s_ap_pass);
#endif

    esp_wifi_stop(); // Best-effort stop of STA mode

    /* Create the default AP netif so the DHCP server runs */
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return;
    }

    /* One initialiser, one identity (CR #4). .authmode defaults to WPA2-PSK so
       a future branch that forgets to set it can never silently open the AP;
       both entry paths then copy the same s_ap_ssid / s_ap_pass below. */
    wifi_config_t ap_config = {
        .ap = {
            .channel = 1,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 4,
        },
    };

    /* Designated initialisers zero the members they do not name, so .ssid and
       .password start empty and need no memset before the copy. ssid_len must
       be set explicitly: the driver does not assume NUL termination. */
    memcpy(ap_config.ap.ssid, s_ap_ssid, strlen(s_ap_ssid));
    ap_config.ap.ssid_len = strlen(s_ap_ssid);
    memcpy(ap_config.ap.password, s_ap_pass, AP_PASS_LEN);

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err == ESP_OK) err = esp_wifi_start();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP mode: %s", esp_err_to_name(err));
        return;
    }

    s_ap_mode = true;
    s_started = true;
    s_connected = false;
    s_ip_str[0] = '\0';

    /* Invariant 2: the mode flag is set LAST -- under the mutex, and only once
       esp_wifi_start() returned ESP_OK and s_ap_mode is true. A reader that
       observes either mode therefore observes a live AP. If the mutex is not
       available the flag stays false: the recovery path then rolls back to STA
       (the entry worker reads the flag), and the setup path re-enters the AP
       every 15/30 min instead of taking the cooldown -- a lost optimisation,
       never a lost AP.

       Setup-AP timeout parity: both APs now arm the same idle/absolute
       one-shot, so an unconfigured device is no longer stranded in the AP
       forever. The flag still records WHICH AP is up, because the shared exit
       worker resolves the two differently (STA vs. cooldown + re-entry). */
    bool published = false;
    if (s_ap_identity_lock &&
        xSemaphoreTake(s_ap_identity_lock, portMAX_DELAY) == pdTRUE) {
        if (recovery) {
            s_recovery_mode = true;
        } else {
            s_setup_mode = true;
        }
        published = true;
        xSemaphoreGive(s_ap_identity_lock);
    } else {
        ESP_LOGE(TAG, "AP up but %s mode not published (no mutex)",
                 recovery ? "recovery" : "setup");
    }

    if (recovery) {
        /* Unchanged: the entry worker arms the timeout after this returns, and
           rolls back to STA when s_recovery_mode stayed false. */
        return;
    }

    /* Setup AP: arm the timeout here. The AP is already up and is the only way
       in for an unconfigured device, so a missing timer is logged and the AP
       stays up -- a lost bound is better than a lost AP. */
    if (!published) {
        /* Without the flag the exit worker cannot tell this AP apart from a
           recovery one, so it will call start() and re-enter at once: a 15/30
           min cycle instead of the 10 min cooldown. Degraded, not stranded. */
        ESP_LOGW(TAG, "setup AP will cycle every 15/30 min: mode not published");
    }
    if (arm_ap_timeout() != ESP_OK) {
        ESP_LOGE(TAG, "setup AP timeout not armed; AP stays up");
    }
}

/* The AP bring-up sequence shared by AP_MODE_SETUP and AP_MODE_RECOVERY:
 * netif stack, default event loop, Wi-Fi driver. Errors are logged but not
 * published; the callers return ESP_FAIL and the workers arm the bring-up
 * retry ladder (design section 6.7). */
static esp_err_t bringup_ap_driver(void)
{
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(e));
        return ESP_FAIL;
    }
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s",
                 esp_err_to_name(e));
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    e = esp_wifi_init(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(e));
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* CR #2 recovery branch of svc_network_start(). Caller has set
 * s_force_recovery. Reuses the shared AP bring-up path with AP_MODE_RECOVERY;
 * start_ap_mode() generates the identity itself (CR #4). */
static esp_err_t start_recovery_ap(void)
{
    if (bringup_ap_driver() != ESP_OK) {
        return ESP_FAIL;
    }

    /* Same partial-failure contract as the setup AP: s_started is set before
       start_ap_mode() so svc_network_stop() can tear down a half-built AP. */
    s_started = true;
    start_ap_mode(AP_MODE_RECOVERY);
    return s_ap_mode ? ESP_OK : ESP_FAIL;
}

esp_err_t svc_network_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    /* D8: the AP identity mutex must exist before start_ap_mode() writes, or a
       getter reads, an identity. Failure here is not fatal for the STA path;
       both AP entry paths fail closed instead (generate_ap_identity()), so no
       provisioning AP can come up without a passphrase. */
    if (!ensure_ap_identity_lock()) {
        ESP_LOGE(TAG, "AP identity mutex unavailable; AP will fail closed");
    }

    /* D8 sibling: the four recovery timers (CR #2). esp_timer_create()
       allocates, so it happens here -- once -- and never in a handler or a
       timer callback. Not fatal for the STA path: every arm site NULL-checks
       its own timer, so a missing timer degrades to "recovery unavailable"
       instead of a broken link (same reasoning as the mutex above). */
    if (!ensure_recovery_timers()) {
        ESP_LOGE(TAG, "recovery timers unavailable; recovery disabled");
    }

    /* Credentials first; missing is non-fatal. */
    char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
    char pass[WIFI_PASS_MAX_LEN + 1] = {0};
    esp_err_t cred_err = read_credentials(ssid, sizeof(ssid),
                                          pass, sizeof(pass));

    /* Design section 6.3: the entry worker (and the bring-up retry worker)
       set s_force_recovery before calling here. This check comes BEFORE the
       missing-credentials branch on purpose: a recovery entry with
       unreadable credentials must still bring up the WPA2 recovery AP, never
       the setup AP. */
    if (s_force_recovery) {
        return start_recovery_ap();
    }

    /* PR #157: an empty SSID is a credential set that can never associate --
       treat it exactly like "unconfigured" instead of burning the STA ladder
       on a guaranteed failure. An empty PASSWORD with a non-empty SSID stays
       valid: that is an open network (see the authmode line below). */
    if (cred_err != ESP_OK || ssid[0] == '\0') {
        /* Setup-AP cooldown: between a setup AP's timeout and its re-entry
           timer the AP must not come back, or every exit would be undone on the
           spot. A start() in that window (bring-up retry, an external caller)
           is deferred to the timer rather than bounced into a fresh entry/exit
           cycle; arm_setup_reentry() never pulls a pending re-entry forward.
           s_started stays false, so the caller's stop() is a harmless no-op and
           the timer's worker starts from a clean driver state. */
        if (s_setup_cooldown_active) {
            ESP_LOGW(TAG, "Setup AP cooling down; re-entry already armed");
            (void)arm_setup_reentry(SVC_NETWORK_COOLDOWN_US);
            return ESP_OK;   /* not started: the re-entry timer owns the link */
        }

        if (cred_err != ESP_OK) {
            ESP_LOGW(TAG, "No WiFi credentials found. Starting AP mode.");
        } else {
            ESP_LOGW(TAG, "Empty SSID stored; treating as unconfigured. "
                          "Starting AP mode.");
        }

        if (bringup_ap_driver() != ESP_OK) {
            /* PR #157: cold-boot bring-up failure. Nothing else would ever
               retry at this point (no worker is running yet), so arm the
               ladder instead of stranding the clock with no link and no AP.
               The retry worker's stop() is a no-op here (s_started is still
               false) and its start() re-runs this same branch. */
            arm_bringup_retry();
            return ESP_FAIL;
        }

        s_started = true; // Allow svc_network_stop() to clean up if AP mode fails
        start_ap_mode(AP_MODE_SETUP);
        if (!s_ap_mode) {
            /* Same cold-boot treatment as above. s_started is already true, so
               the retry worker's stop() tears the half-built AP down --
               including esp_wifi_deinit(), which the next bringup_ap_driver()
               requires -- before start() tries again. */
            arm_bringup_retry();
            return ESP_FAIL;
        }
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
    /* Copy the full field width. A 32-char SSID and a 64-char (hex PSK)
       password are valid; ESP-IDF does not need a NUL when the field is
       full, and wcfg was zeroed above so shorter values stay terminated.
       (The old "sizeof - 1" silently truncated those to 31 / 63 chars.) */
    memcpy(wcfg.sta.ssid,     ssid, strnlen(ssid, sizeof(wcfg.sta.ssid)));
    memcpy(wcfg.sta.password, pass, strnlen(pass, sizeof(wcfg.sta.password)));
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
        /* PR #157: cold-boot bring-up failure. s_started is set before the
           return on purpose -- the driver is initialised and the event
           handlers and the STA netif are live, so svc_network_stop() must be
           allowed to tear them down. Without it the retry worker's stop() is
           a no-op and its start() dies on esp_wifi_init()'s
           ESP_ERR_WIFI_INIT_STATE forever, which would turn "bounded retries"
           into "bounded failures". Same partial-failure contract as the
           setup-AP branch above. */
        s_started = true;
        arm_bringup_retry();
        return ESP_FAIL;
    }

    s_started = true;

    /* E1 (design section 6.7): the bring-up ladder is reset on every
       successful bring-up -- here for the STA path; the entry worker resets it
       after a successful recovery AP start (section 6.3). Never in
       reset_backoff() or svc_network_stop(), both of which run on every
       teardown and would pin the ladder at 1 s.
       F4 (design section 6.2): a successful restart also ends any auth-fail
       streak. Without this the counter would still sit at the threshold when
       the STA path resumes after a recovery cycle, so the next single
       auth-class failure would re-trigger recovery immediately. The
       any-reason counter (PR #157) is reset for the same reason: a successful
       bring-up must not leave the next single disconnect one step away from
       the slow-path trigger. */
    s_bringup_backoff_ms    = SVC_NETWORK_BACKOFF_MIN_MS;
    s_consecutive_auth_fail = 0;
    s_consecutive_fail      = 0;

    ESP_LOGI(TAG, "WiFi STA starting");
    publish_network_event(EVENT_NETWORK_STARTING, NULL, 0);
    return ESP_OK;
}

esp_err_t svc_network_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    /* Invariant 2 (design section 5): clear the mode flags FIRST, under the
       AP identity mutex, before any teardown begins, and clear the AP identity
       with it -- it is ephemeral and regenerated at every entry (CR #4). The
       getters gate on s_ap_mode and on the identity itself, so they fail
       closed before the interface they describe starts to disappear.

       Note (CR #4): the HTTP carve-out keys on s_ap_mode via
       provisioning_exempt(), so it stays open until s_ap_mode is cleared below.
       That is deliberate: the AP is still WPA2 while it is torn down, and the
       exempted paths are the commissioning endpoints.

       If the mutex is missing we still clear, unlocked, because leaving the
       identity readable is the unsafe direction. */
    bool locked = (s_ap_identity_lock != NULL) &&
                  (xSemaphoreTake(s_ap_identity_lock, portMAX_DELAY) == pdTRUE);
    s_recovery_mode = false;
    s_setup_mode = false;
    s_ap_ssid[0] = '\0';
    s_ap_pass[0] = '\0';
    if (locked) {
        xSemaphoreGive(s_ap_identity_lock);
    } else {
        ESP_LOGE(TAG, "AP identity mutex unavailable; identity cleared unlocked");
    }

    /* D3: unregister the event handlers BEFORE esp_wifi_stop/deinit. Stop
       itself emits STA_DISCONNECTED; a still-registered handler would re-arm
       s_reconnect_timer past the deinit, and that callback would then call
       svc_network_start() on a deinitialised driver. */
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

    /* One-shot timers next, so no callback can spawn a worker into a
       half-torn-down stack. s_cooldown_timer is deliberately NOT stopped: the
       cooldown must survive stop/start cycles (section 6.6), otherwise the
       exit worker's re-trigger protection dies on the next stop.
       s_setup_reentry_timer is NOT stopped either, for the same reason and
       more strongly: the setup exit worker arms it right after calling this
       function, and stopping it here would let any later stop() (bring-up
       retry, a caller) cancel the only path back to a usable AP.
       esp_timer_stop() reporting ESP_ERR_INVALID_STATE (not armed) is the
       normal case and is ignored. */
    if (s_reconnect_timer) {
        esp_timer_stop(s_reconnect_timer);
    }
    if (s_recovery_entry_trigger_timer) {
        esp_timer_stop(s_recovery_entry_trigger_timer);
    }
    if (s_recovery_timeout_timer) {
        esp_timer_stop(s_recovery_timeout_timer);
    }
    if (s_bringup_retry_timer) {
        esp_timer_stop(s_bringup_retry_timer);
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    /* B7: s_sta_netif was never destroyed, so every stop/start cycle leaked
       one netif. svc_network_start() re-creates it when it is NULL. */
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }

    s_connected = false;
    s_ip_str[0] = '\0';
    s_started = false;
    s_ap_mode = false;
    reset_backoff();

    /* D6: bump the generation LAST, so a worker that snapshotted
       s_generation before this teardown notices the restart and stands down
       instead of overruling it. s_recovery_active and s_force_recovery are
       NOT touched here: both are owned by the workers (section 6.3, section
       5 invariant 3) and s_force_recovery in particular must survive this
       call for the entry worker's stop() -> start() to reach the recovery
       branch. s_cooldown_active is likewise left alone (section 6.6), and so
       is s_setup_cooldown_active: the setup exit worker sets it after this
       function returns and only the re-entry worker clears it. */
    s_generation++;

    ESP_LOGI(TAG, "WiFi stopped");
    return ESP_OK;
}

bool svc_network_is_connected(void)
{
    return s_connected;
}

/* True while the provisioning AP is up. CR #4 unified the two scenarios this
   AP serves -- no credentials stored (setup) and a credential lockout
   (recovery) -- into one identity, so a caller cannot and need not tell them
   apart: the SSID is "ChronoLink-<MAC4>" either way, and both are WPA2-PSK
   with a per-entry passphrase. Plain bool read, deliberately unlocked: httpd
   calls it on every request and ui_producer every 250 ms, and a value one tick
   stale is harmless to both. */
bool svc_network_is_ap_mode(void)
{
    return s_ap_mode;
}

const char *svc_network_get_ip_str(void)
{
    return s_connected ? s_ip_str : NULL;
}

/* ------------------------------------------------------------------ */
/* CR #4: provisioning AP public API                                  */
/* ------------------------------------------------------------------ */

/* Both getters return the identity of the AP that is currently up; there is no
   setup/recovery distinction to make any more. On any error out[] is left
   untouched, so a caller that ignores the return value can never read a stale
   or truncated SSID/passphrase as if it were the current one. */

esp_err_t svc_network_get_ap_ssid(char *out, size_t cap)
{
    /* s_ap_ssid is written only by generate_ap_identity()/svc_network_stop()
       on the bringing-up task, and read here without the mutex: the copy is of
       a NUL-terminated string that is at most 15 chars, and the s_ap_mode test
       below makes a caller ignore any result taken across a rotation. */
    if (!out || cap < strlen(s_ap_ssid) + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ap_mode || s_ap_ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(out, s_ap_ssid, cap);
    return ESP_OK;
}

esp_err_t svc_network_get_ap_passphrase(char *out, size_t cap)
{
    /* cap is checked against the full buffer, not against strlen(), so an
       identity that is still empty reports ESP_ERR_INVALID_STATE below rather
       than looking like a short passphrase. */
    if (!out || cap < AP_PASS_BUF_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ap_identity_lock ||
        xSemaphoreTake(s_ap_identity_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_ap_mode || s_ap_pass[0] == '\0') {
        xSemaphoreGive(s_ap_identity_lock);
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(out, s_ap_pass, cap);
    xSemaphoreGive(s_ap_identity_lock);
    return ESP_OK;
}

void svc_network_touch_provisioning_idle_timeout(void)
{
    /* E2: this function and arm_ap_timeout() (which seeds the value at every
       entry) are the only writers of s_last_recovery_activity_us, and both
       hold s_ap_identity_lock. Never stop or start a timer from here:
       re-arming is the timeout callback's job (D7), and doing it from an HTTP
       request would let a request storm push the absolute cap out
       indefinitely.
       Covers BOTH provisioning APs (setup-AP timeout parity): start_ap_mode()
       publishes s_recovery_mode / s_setup_mode only after esp_wifi_start()
       succeeded, so the flag pair is exactly "an AP with an armed idle timeout
       is up". Without the setup half, a request during a setup session would
       not extend the 15 min window and the panel's "Auto-exits when idle"
       would be untrue. */
    if (!s_ap_identity_lock ||
        xSemaphoreTake(s_ap_identity_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "idle-timeout touch dropped (no mutex)");
        return;
    }
    if (s_recovery_mode || s_setup_mode) {
        s_last_recovery_activity_us = esp_timer_get_time();
    }
    xSemaphoreGive(s_ap_identity_lock);
}

