/*
 * svc_config.c
 *
 * ChronoLink V4 OS -- configuration service (implementation)
 *
 * See svc_config.h for the schema and the consumer status of each field.
 */

#include "svc_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>     /* snprintf: composed NVS keys (jamat_*, season_*) */
#include <string.h>

static const char *TAG = "svc_config";

#define NVS_NAMESPACE_CONFIG   "config"
#define NVS_KEY_LOCATION_NAME  "loc_name"
#define NVS_KEY_LAT_UDEG       "lat_udeg"   /* int32 microdegrees */
#define NVS_KEY_LON_UDEG       "lon_udeg"
#define NVS_KEY_METHOD         "method"     /* uint8 */
#define NVS_KEY_MADHAB         "madhab"     /* uint8 */
#define NVS_KEY_CUST_FAJR      "cust_f_ang" /* int32 milli-degrees */
#define NVS_KEY_CUST_ISHA      "cust_i_ang" /* int32 milli-degrees */
#define NVS_KEY_ADHAN_MODE     "adhan_mode"
#define NVS_KEY_ADHAN_VOL      "adhan_vol"  /* uint8, 0..100 */
#define NVS_KEY_BRIGHT_MODE    "bright_mode"
#define NVS_KEY_AUTO_BRIGHT    "auto_bright"
#define NVS_KEY_NIGHT_LAMP     "night_lamp"
#define NVS_KEY_HIGH_LAT       "high_lat"
#define NVS_KEY_ZAWAL_BUF      "zawal_buf"
/* Seasonal jamat model (5.1a). Per-prayer, per-season anchors/offsets,
   one shared rounding per prayer. Old iqamah_fajr.. keys stay readable
   for migration and downgrade (never erased). */
#define NVS_KEY_JAMAT_ANCHOR_W  "jamat_anc_w_"    /* + "0".."4" */
#define NVS_KEY_JAMAT_ANCHOR_S  "jamat_anc_s_"    /* + "0".."4" */
#define NVS_KEY_JAMAT_OFF_W     "jamat_off_w_"    /* + "0".."4" */
#define NVS_KEY_JAMAT_OFF_S     "jamat_off_s_"    /* + "0".."4" */
#define NVS_KEY_JAMAT_ROUND     "jamat_rnd_"      /* + "0".."4" */
#define NVS_KEY_SEASON_SUMMER_M "season_sum_m"
#define NVS_KEY_SEASON_SUMMER_D "season_sum_d"
#define NVS_KEY_SEASON_WINTER_M "season_win_m"
#define NVS_KEY_SEASON_WINTER_D "season_win_d"
static const char *const k_adhan_offset_keys[SVC_CONFIG_PRAYER_COUNT] = {
    "adhan_off_0",
    "adhan_off_1",
    "adhan_off_2",
    "adhan_off_3",
    "adhan_off_4",
};

static SemaphoreHandle_t s_lock    = NULL;
static bool              s_nvs_open = false;
static nvs_handle_t      s_nvs;
static svc_config_t      s_cache;

static const char *const k_iqamah_keys[SVC_CONFIG_PRAYER_COUNT] = {
    "iqamah_fajr",
    "iqamah_dhuhr",
    "iqamah_asr",
    "iqamah_maghrib",
    "iqamah_isha",
};

/* Helper: set one prayer's jamat for both seasons. */
static void preset_jamat_both(jamat_per_prayer_cfg_t *j,
                              svc_config_jamat_anchor_t w_anchor, uint16_t w_off,
                              svc_config_jamat_anchor_t s_anchor, uint16_t s_off,
                              uint8_t round_min)
{
    j->winter.anchor     = (uint8_t)w_anchor;
    j->winter.offset_min = w_off;
    j->summer.anchor     = (uint8_t)s_anchor;
    j->summer.offset_min = s_off;
    j->round_min         = round_min;
}

/* UK preset. Fajr uses SUNRISE_MINUS (45 min before sunrise) in winter
   and is DISABLED in summer (sunrise too early). Dhuhr winter +70,
   summer +25 so jamat lands ~13:15-13:30 year-round. Maghrib round 2. */
static void apply_uk_preset(svc_config_t *c)
{
    c->method        = SVC_CONFIG_METHOD_MWL;
    c->madhab        = SVC_CONFIG_MADHAB_STANDARD;
    c->high_lat_rule = 0;

    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_FAJR],
                      SVC_CONFIG_JAMAT_SUNRISE_MINUS, 45,
                      SVC_CONFIG_JAMAT_DISABLED,      0, 5);
    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_DHUHR],
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 70,
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 25, 5);
    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_ASR],
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 15,
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 15, 5);
    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_MAGHRIB],
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 3,
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 3, 2);
    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_ISHA],
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 15,
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 15, 5);

    c->season.summer_start_month = 3;
    c->season.summer_start_day   = 25;
    c->season.winter_start_month = 10;
    c->season.winter_start_day   = 25;

    for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT; i++) {
        c->iqamah_offset_min[i] = c->jamat[i].winter.offset_min;
    }
}

/* Pakistan preset. Karachi method, Hanafi madhab. Dhuhr jamat is a
   fixed 13:30 clock time in practice; expressed here as ADHAN_PLUS +70
   because astronomical Dhuhr is ~12:20 there. Same offset both seasons
   (Pakistan has no DST, so the mosque notice doesn't switch). */
static void apply_pakistan_preset(svc_config_t *c)
{
    c->method        = SVC_CONFIG_METHOD_KARACHI;
    c->madhab        = SVC_CONFIG_MADHAB_HANAFI;
    c->high_lat_rule = 0;

    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_FAJR],
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 20,
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 20, 5);
    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_DHUHR],
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 70,
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 70, 5);
    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_ASR],
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 15,
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 15, 5);
    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_MAGHRIB],
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 3,
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 3, 2);
    preset_jamat_both(&c->jamat[SVC_CONFIG_PRAYER_ISHA],
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 15,
                      SVC_CONFIG_JAMAT_ADHAN_PLUS, 15, 5);

    c->season.summer_start_month = 3;
    c->season.summer_start_day   = 25;
    c->season.winter_start_month = 10;
    c->season.winter_start_day   = 25;

    for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT; i++) {
        c->iqamah_offset_min[i] = c->jamat[i].winter.offset_min;
    }
}

static void apply_uk_moonsighting_preset(svc_config_t *c)
{
    apply_uk_preset(c);
    c->method = SVC_CONFIG_METHOD_MOONSIGHTING;
}

static void apply_preset_unlocked(svc_config_t *c, svc_config_preset_t preset)
{
    switch (preset) {
    case SVC_CONFIG_PRESET_PAKISTAN:        apply_pakistan_preset(c);        break;
    case SVC_CONFIG_PRESET_UK_MOONSIGHTING: apply_uk_moonsighting_preset(c); break;
    case SVC_CONFIG_PRESET_UK:
    default:                                apply_uk_preset(c);              break;
    }
}

bool svc_config_round_min_is_valid(uint8_t round_min)
{
    switch (round_min) {
    case 0: case 2: case 5: case 10: case 15: case 20: case 30:
        return true;
    default:
        return false;
    }
}

/* Factory defaults = UK preset, on top of the non-jamat fields. */
static void set_defaults(svc_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->location_valid    = false;
    c->method            = SVC_CONFIG_METHOD_MWL;
    c->madhab            = SVC_CONFIG_MADHAB_STANDARD;
    c->custom_fajr_angle = 18.0;
    c->custom_isha_angle = 18.0;
    c->adhan_mode        = SVC_CONFIG_ADHAN_OFF;
    c->adhan_volume      = 70;
    c->brightness_mode   = 0;
    c->high_lat_rule     = 0;
    c->zawal_buffer_min  = 10;
    for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT; i++) {
        c->adhan_offset_min[i] = 0;
    }
    c->auto_brightness   = false;
    c->night_lamp_enable = false;
    apply_preset_unlocked(c, SVC_CONFIG_PRESET_UK);
}

/* --------------------------------------------------------------------------
 * Lock helpers
 *
 * Every public function that reads or writes s_cache or s_nvs takes the
 * lock. Setters hold it across both the NVS write and the cache update so
 * a reader never sees a half-applied change.
 * -------------------------------------------------------------------------- */

static bool lock_take(void)
{
    if (!s_lock) return false;
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void lock_give(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* Persist a single NVS entry. Caller holds the lock. If NVS is not open,
   returns ESP_ERR_NVS_NOT_INITIALIZED without touching anything -- the
   caller has already updated the cache, so RAM state is still valid. */
static esp_err_t persist_u8(const char *key, uint8_t v)
{
    if (!s_nvs_open) return ESP_ERR_NVS_NOT_INITIALIZED;
    esp_err_t err = nvs_set_u8(s_nvs, key, v);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

static esp_err_t persist_i32(const char *key, int32_t v)
{
    if (!s_nvs_open) return ESP_ERR_NVS_NOT_INITIALIZED;
    esp_err_t err = nvs_set_i32(s_nvs, key, v);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

static esp_err_t persist_str(const char *key, const char *v)
{
    if (!s_nvs_open) return ESP_ERR_NVS_NOT_INITIALIZED;
    esp_err_t err = nvs_set_str(s_nvs, key, v);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

/* Erase a single NVS entry. Caller holds the lock. Missing key is
   treated as success (the desired end-state -- the key is gone -- is
   already true). Returns ESP_ERR_NVS_NOT_INITIALIZED if NVS is not
   open, matching persist_u8/i32/str. */
static esp_err_t persist_erase(const char *key)
{
    if (!s_nvs_open) return ESP_ERR_NVS_NOT_INITIALIZED;
    esp_err_t err = nvs_erase_key(s_nvs, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

/* Load all keys from NVS into s_cache. Called from svc_config_start()
   while it is still single-threaded (no lock exists yet). Non-fatal: a
   missing key keeps its set_defaults() value. A malformed value (e.g.
   wrong length string, or a u8 outside the enum range) is logged and
   the default is kept. */
static void load_from_nvs_unlocked(void)
{
    if (!s_nvs_open) return;

    size_t len = sizeof(s_cache.location_name);
    esp_err_t err = nvs_get_str(s_nvs, NVS_KEY_LOCATION_NAME,
                                s_cache.location_name, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "load '%s' failed: %s",
                 NVS_KEY_LOCATION_NAME, esp_err_to_name(err));
    }

    int32_t lat_udeg = 0, lon_udeg = 0;
    esp_err_t lat_err = nvs_get_i32(s_nvs, NVS_KEY_LAT_UDEG, &lat_udeg);
    esp_err_t lon_err = nvs_get_i32(s_nvs, NVS_KEY_LON_UDEG, &lon_udeg);
    if (lat_err == ESP_OK && lon_err == ESP_OK) {
        s_cache.latitude  = (double)lat_udeg / 1000000.0;
        s_cache.longitude = (double)lon_udeg / 1000000.0;
        s_cache.location_valid = true;
    }
    uint8_t v = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_METHOD, &v) == ESP_OK
        && v < SVC_CONFIG_METHOD_COUNT) {
        s_cache.method = (svc_config_method_t)v;
    } else if (v >= SVC_CONFIG_METHOD_COUNT) {
        ESP_LOGW(TAG, "stored method %u out of range -- using default", v);
    }

    int32_t cust_ang = 0;
    if (nvs_get_i32(s_nvs, NVS_KEY_CUST_FAJR, &cust_ang) == ESP_OK && cust_ang >= 0 && cust_ang <= 30000) {
        s_cache.custom_fajr_angle = (double)cust_ang / 1000.0;
    }
    if (nvs_get_i32(s_nvs, NVS_KEY_CUST_ISHA, &cust_ang) == ESP_OK && cust_ang >= 0 && cust_ang <= 30000) {
        s_cache.custom_isha_angle = (double)cust_ang / 1000.0;
    }

    v = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_MADHAB, &v) == ESP_OK
        && v < SVC_CONFIG_MADHAB_COUNT) {
        s_cache.madhab = (svc_config_madhab_t)v;
    } else if (v >= SVC_CONFIG_MADHAB_COUNT) {
        ESP_LOGW(TAG, "stored madhab %u out of range -- using default", v);
    }

    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        uint16_t off = 0;
        if (nvs_get_u16(s_nvs, k_iqamah_keys[p], &off) == ESP_OK) {
            s_cache.iqamah_offset_min[p] = off;
            /* Migrate: pre-5.1a offset becomes both seasons' offset
               (anchor ADHAN_PLUS, defaults already set by preset). */
            s_cache.jamat[p].winter.offset_min = off;
            s_cache.jamat[p].summer.offset_min = off;
        }
    }

    /* New 5.1a keys override the preset and override the migration above. */
    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        char key[24];
        uint8_t v8 = 0;
        uint16_t v16 = 0;

        /* Each read resets its temporary first: nvs_get_* leaves the
           output untouched on failure, so a stale value would otherwise
           be validated and applied. */
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_ANCHOR_W, p);
        if (nvs_get_u8(s_nvs, key, &v8) == ESP_OK
            && v8 < SVC_CONFIG_JAMAT_ANCHOR_COUNT) {
            s_cache.jamat[p].winter.anchor = v8;
        }
        v8 = 0;
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_ANCHOR_S, p);
        if (nvs_get_u8(s_nvs, key, &v8) == ESP_OK
            && v8 < SVC_CONFIG_JAMAT_ANCHOR_COUNT) {
            s_cache.jamat[p].summer.anchor = v8;
        }
        v16 = 0;
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_OFF_W, p);
        if (nvs_get_u16(s_nvs, key, &v16) == ESP_OK && v16 <= 1440) {
            s_cache.jamat[p].winter.offset_min = v16;
        }
        v16 = 0;
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_OFF_S, p);
        if (nvs_get_u16(s_nvs, key, &v16) == ESP_OK && v16 <= 1440) {
            s_cache.jamat[p].summer.offset_min = v16;
        }
        v8 = 0;
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_ROUND, p);
        if (nvs_get_u8(s_nvs, key, &v8) == ESP_OK
            && svc_config_round_min_is_valid(v8)) {
            s_cache.jamat[p].round_min = v8;
        }
    }

    uint8_t sv = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_SEASON_SUMMER_M, &sv) == ESP_OK
        && sv >= 1 && sv <= 12) {
        s_cache.season.summer_start_month = sv;
    }
    sv = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_SEASON_SUMMER_D, &sv) == ESP_OK
        && sv >= 1 && sv <= 31) {
        s_cache.season.summer_start_day = sv;
    }
    sv = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_SEASON_WINTER_M, &sv) == ESP_OK
        && sv >= 1 && sv <= 12) {
        s_cache.season.winter_start_month = sv;
    }
    sv = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_SEASON_WINTER_D, &sv) == ESP_OK
        && sv >= 1 && sv <= 31) {
        s_cache.season.winter_start_day = sv;
    }

    /* Refresh legacy mirror from the loaded winter offsets so the
       pre-5.1a GET shape stays coherent. */
    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        s_cache.iqamah_offset_min[p] = s_cache.jamat[p].winter.offset_min;
    }

    v = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_ADHAN_MODE, &v) == ESP_OK
        && v < SVC_CONFIG_ADHAN_MODE_COUNT) {
        s_cache.adhan_mode = (svc_config_adhan_mode_t)v;
    } else if (v >= SVC_CONFIG_ADHAN_MODE_COUNT) {
        ESP_LOGW(TAG, "stored adhan_mode %u out of range -- using default", v);
    }

    v = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_ADHAN_VOL, &v) == ESP_OK && v <= 100) {
        s_cache.adhan_volume = v;
    } else if (v > 100) {
        ESP_LOGW(TAG, "stored adhan_volume %u out of range -- using default", v);
    }

    v = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_BRIGHT_MODE, &v) == ESP_OK && v <= 2) {
        s_cache.brightness_mode = v;
    } else if (v > 2) {
        ESP_LOGW(TAG, "stored brightness_mode %u out of range -- using default", v);
    }

    v = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_HIGH_LAT, &v) == ESP_OK && v <= 4) {
        s_cache.high_lat_rule = v;
    } else if (v > 4) {
        ESP_LOGW(TAG, "stored high_lat_rule %u out of range -- using default", v);
    }

    uint16_t off = 0;
    for (int i = 0; i < SVC_CONFIG_PRAYER_COUNT; i++) {
        if (nvs_get_u16(s_nvs, k_adhan_offset_keys[i], &off) == ESP_OK && off <= 60) {
            s_cache.adhan_offset_min[i] = off;
        }
    }

    off = 0;
    if (nvs_get_u16(s_nvs, NVS_KEY_ZAWAL_BUF, &off) == ESP_OK && off <= 30) {
        s_cache.zawal_buffer_min = off;
    }

    v = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_AUTO_BRIGHT, &v) == ESP_OK) {
        s_cache.auto_brightness = (v != 0);
    }

    v = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_NIGHT_LAMP, &v) == ESP_OK) {
        s_cache.night_lamp_enable = (v != 0);
    }
}

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

esp_err_t svc_config_start(void)
{
    if (s_lock != NULL) {
        return ESP_OK;   /* idempotent */
    }

    set_defaults(&s_cache);

    esp_err_t err = nvs_open(NVS_NAMESPACE_CONFIG, NVS_READWRITE, &s_nvs);
    if (err == ESP_OK) {
        s_nvs_open = true;
    } else {
        s_nvs_open = false;
        ESP_LOGW(TAG, "NVS namespace '%s' unavailable (%s) -- using defaults",
                 NVS_NAMESPACE_CONFIG, esp_err_to_name(err));
    }

    load_from_nvs_unlocked();

    /* Create the mutex LAST. Its existence is the "fully initialised"
       flag: any getter/setter reaching here before this line would see
       !s_lock and return ESP_ERR_INVALID_STATE, rather than reading
       half-loaded defaults. */
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "Failed to create config mutex");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "started (NVS %s)", s_nvs_open ? "open" : "unavailable");
    return ESP_OK;
}

esp_err_t svc_config_get(svc_config_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;

    if (!lock_take()) return ESP_ERR_TIMEOUT;
    *out = s_cache;
    lock_give();
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Setters
 *
 * Every setter:
 *   1. Returns ESP_ERR_INVALID_STATE if svc_config_start() has not run.
 *   2. Takes the lock (returns ESP_ERR_TIMEOUT if it cannot).
 *   3. Updates s_cache.
 *   4. Persists to NVS (if NVS is open).
 *   5. Releases the lock.
 *   6. Returns the NVS result; when NVS is unavailable this is
 *      ESP_ERR_NVS_NOT_INITIALIZED even though RAM was updated (see
 *      svc_config.h).
 * -------------------------------------------------------------------------- */

esp_err_t svc_config_set_location_name(const char *name)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (!name) return ESP_ERR_INVALID_ARG;

    size_t len = strlen(name);
    if (len >= SVC_CONFIG_NAME_MAX) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    /* len was range-checked above to be < SVC_CONFIG_NAME_MAX, so
       len+1 bytes (including the NUL) always fit. memcpy avoids the
       -Wstringop-truncation warning GCC emits for strncpy even when
       the result is NUL-terminated. */
    memcpy(s_cache.location_name, name, len + 1);

    esp_err_t err = persist_str(NVS_KEY_LOCATION_NAME, s_cache.location_name);
    lock_give();
    return err;
}

esp_err_t svc_config_set_location(double lat, double lon)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;

    /* Range check: lat in [-90, 90], lon in [-180, 180]. No tolerance
       is needed on the upper edge: microdegree quantization rounds
       into range (89.9999999 deg -> 90.000000 deg exactly). */
    /* NaN-safe: "! (lat >= -90 && lat <= 90)" is true for NaN, unlike
       the equivalent negated comparison, so NaN cannot reach the
       double-to-int32 conversion below (which would be UB). */
    if (!(lat >= -90.0 && lat <= 90.0)) return ESP_ERR_INVALID_ARG;
    if (!(lon >= -180.0 && lon <= 180.0)) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.latitude  = lat;
    s_cache.longitude = lon;
    s_cache.location_valid = true;

    /* NVS schema: microdegrees as int32_t. ESP32 is little-endian and
       doesn't need __builtin_bswap -- NVS i32 is host-endian. */
    int32_t lat_udeg = (int32_t)(lat * 1000000.0 + (lat >= 0 ? 0.5 : -0.5));
    int32_t lon_udeg = (int32_t)(lon * 1000000.0 + (lon >= 0 ? 0.5 : -0.5));

    esp_err_t lat_err = persist_i32(NVS_KEY_LAT_UDEG, lat_udeg);
    esp_err_t lon_err = persist_i32(NVS_KEY_LON_UDEG, lon_udeg);
    lock_give();

    /* Return the first non-OK result so callers see a partial write. */
    if (lat_err != ESP_OK) return lat_err;
    return lon_err;
}

esp_err_t svc_config_clear_location(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.latitude  = 0.0;
    s_cache.longitude = 0.0;
    s_cache.location_valid = false;

    esp_err_t lat_err = persist_erase(NVS_KEY_LAT_UDEG);
    esp_err_t lon_err = persist_erase(NVS_KEY_LON_UDEG);
    lock_give();

    if (lat_err != ESP_OK) return lat_err;
    return lon_err;
}

/* Helper for uint16_t NVS persistence. Required for iqamah offset
   (minutes), which is stored as u16 to allow > 255 min offsets if
   ever needed. */
static esp_err_t persist_u16(const char *key, uint16_t v)
{
    if (!s_nvs_open) return ESP_ERR_NVS_NOT_INITIALIZED;
    esp_err_t err = nvs_set_u16(s_nvs, key, v);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

esp_err_t svc_config_set_method(svc_config_method_t m)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if ((unsigned)m >= (unsigned)SVC_CONFIG_METHOD_COUNT) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.method = m;
    esp_err_t err = persist_u8(NVS_KEY_METHOD, (uint8_t)m);
    lock_give();
    return err;
}

esp_err_t svc_config_set_madhab(svc_config_madhab_t m)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if ((unsigned)m >= (unsigned)SVC_CONFIG_MADHAB_COUNT) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.madhab = m;
    esp_err_t err = persist_u8(NVS_KEY_MADHAB, (uint8_t)m);
    lock_give();
    return err;
}

/* DEPRECATED — preserves pre-5.1a behaviour by writing the same offset
   to both seasons. New code should use svc_config_set_jamat_season(). */
esp_err_t svc_config_set_iqamah_offset(svc_config_prayer_t p, uint16_t minutes)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if ((unsigned)p >= (unsigned)SVC_CONFIG_PRAYER_COUNT) return ESP_ERR_INVALID_ARG;
    if (minutes > 1440) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.iqamah_offset_min[p]       = minutes;
    s_cache.jamat[p].winter.offset_min = minutes;
    s_cache.jamat[p].summer.offset_min = minutes;

    esp_err_t e1 = persist_u16(k_iqamah_keys[p], minutes);
    char key[24];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_OFF_W, p);
    esp_err_t e2 = persist_u16(key, minutes);
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_OFF_S, p);
    esp_err_t e3 = persist_u16(key, minutes);

    lock_give();
    if (e1 != ESP_OK) return e1;
    if (e2 != ESP_OK) return e2;
    return e3;
}

esp_err_t svc_config_set_adhan_mode(svc_config_adhan_mode_t mode)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if ((unsigned)mode >= (unsigned)SVC_CONFIG_ADHAN_MODE_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.adhan_mode = mode;
    esp_err_t err = persist_u8(NVS_KEY_ADHAN_MODE, (uint8_t)mode);
    lock_give();
    return err;
}

esp_err_t svc_config_set_adhan_volume(uint8_t volume_0_100)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (volume_0_100 > 100) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.adhan_volume = volume_0_100;
    esp_err_t err = persist_u8(NVS_KEY_ADHAN_VOL, volume_0_100);
    lock_give();
    return err;
}

esp_err_t svc_config_set_brightness_mode(uint8_t mode)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (mode > 2) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.brightness_mode = mode;
    esp_err_t err = persist_u8(NVS_KEY_BRIGHT_MODE, mode);
    lock_give();
    return err;
}

esp_err_t svc_config_set_auto_brightness(bool enabled)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.auto_brightness = enabled;
    esp_err_t err = persist_u8(NVS_KEY_AUTO_BRIGHT, enabled ? 1 : 0);
    lock_give();
    return err;
}

esp_err_t svc_config_set_night_lamp(bool enabled)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.night_lamp_enable = enabled;
    esp_err_t err = persist_u8(NVS_KEY_NIGHT_LAMP, enabled ? 1 : 0);
    lock_give();
    return err;
}

esp_err_t svc_config_set_high_lat_rule(uint8_t rule)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (rule > 4) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.high_lat_rule = rule;
    esp_err_t err = persist_u8(NVS_KEY_HIGH_LAT, rule);
    lock_give();
    return err;
}

esp_err_t svc_config_set_adhan_offset(svc_config_prayer_t p, uint16_t minutes)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if ((unsigned)p >= (unsigned)SVC_CONFIG_PRAYER_COUNT) return ESP_ERR_INVALID_ARG;
    if (minutes > 60) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.adhan_offset_min[p] = minutes;
    esp_err_t err = persist_u16(k_adhan_offset_keys[p], minutes);
    lock_give();
    return err;
}

esp_err_t svc_config_set_zawal_buffer(uint16_t minutes)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (minutes > 30) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.zawal_buffer_min = minutes;
    esp_err_t err = persist_u16(NVS_KEY_ZAWAL_BUF, minutes);
    lock_give();
    return err;
}

/* -------------------------------------------------------------------------
 * Seasonal jamat setters (5.1a)
 * ------------------------------------------------------------------------- */

esp_err_t svc_config_set_jamat_season(svc_config_prayer_t p, uint8_t season,
                                      svc_config_jamat_anchor_t anchor,
                                      uint16_t offset_min)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if ((unsigned)p >= (unsigned)SVC_CONFIG_PRAYER_COUNT) return ESP_ERR_INVALID_ARG;
    if (season > 1) return ESP_ERR_INVALID_ARG;
    if ((unsigned)anchor >= (unsigned)SVC_CONFIG_JAMAT_ANCHOR_COUNT) return ESP_ERR_INVALID_ARG;
    if (offset_min > 1440) return ESP_ERR_INVALID_ARG;
    /* Anchor validation: SUNRISE_MINUS only Fajr, MIDNIGHT_MINUS only Isha. */
    if (anchor == SVC_CONFIG_JAMAT_SUNRISE_MINUS
        && p != SVC_CONFIG_PRAYER_FAJR) return ESP_ERR_INVALID_ARG;
    if (anchor == SVC_CONFIG_JAMAT_MIDNIGHT_MINUS
        && p != SVC_CONFIG_PRAYER_ISHA) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    jamat_season_cfg_t *dst = (season == 0)
        ? &s_cache.jamat[p].winter : &s_cache.jamat[p].summer;
    dst->anchor     = (uint8_t)anchor;
    dst->offset_min = offset_min;

    char key[24];
    esp_err_t e1, e2;
    if (season == 0) {
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_ANCHOR_W, p);
        e1 = persist_u8(key, (uint8_t)anchor);
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_OFF_W, p);
        e2 = persist_u16(key, offset_min);
        /* Legacy mirror update for winter. */
        s_cache.iqamah_offset_min[p] = offset_min;
        (void)persist_u16(k_iqamah_keys[p], offset_min);
    } else {
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_ANCHOR_S, p);
        e1 = persist_u8(key, (uint8_t)anchor);
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_OFF_S, p);
        e2 = persist_u16(key, offset_min);
    }
    lock_give();
    return (e1 != ESP_OK) ? e1 : e2;
}

esp_err_t svc_config_set_jamat_round(svc_config_prayer_t p, uint8_t round_min)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if ((unsigned)p >= (unsigned)SVC_CONFIG_PRAYER_COUNT) return ESP_ERR_INVALID_ARG;
    if (!svc_config_round_min_is_valid(round_min)) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.jamat[p].round_min = round_min;

    char key[24];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_ROUND, p);
    esp_err_t err = persist_u8(key, round_min);
    lock_give();
    return err;
}

esp_err_t svc_config_set_season(uint8_t summer_m, uint8_t summer_d,
                                uint8_t winter_m, uint8_t winter_d)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (summer_m < 1 || summer_m > 12 || summer_d < 1 || summer_d > 31) return ESP_ERR_INVALID_ARG;
    if (winter_m < 1 || winter_m > 12 || winter_d < 1 || winter_d > 31) return ESP_ERR_INVALID_ARG;
    if (summer_m == winter_m && summer_d == winter_d) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.season.summer_start_month = summer_m;
    s_cache.season.summer_start_day   = summer_d;
    s_cache.season.winter_start_month = winter_m;
    s_cache.season.winter_start_day   = winter_d;

    esp_err_t e1 = persist_u8(NVS_KEY_SEASON_SUMMER_M, summer_m);
    esp_err_t e2 = persist_u8(NVS_KEY_SEASON_SUMMER_D, summer_d);
    esp_err_t e3 = persist_u8(NVS_KEY_SEASON_WINTER_M, winter_m);
    esp_err_t e4 = persist_u8(NVS_KEY_SEASON_WINTER_D, winter_d);
    lock_give();
    if (e1 != ESP_OK) return e1;
    if (e2 != ESP_OK) return e2;
    if (e3 != ESP_OK) return e3;
    return e4;
}

esp_err_t svc_config_apply_preset(svc_config_preset_t preset)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if ((unsigned)preset >= (unsigned)SVC_CONFIG_PRESET_COUNT) return ESP_ERR_INVALID_ARG;

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    apply_preset_unlocked(&s_cache, preset);

    /* Persist everything the preset touches. Failures are non-fatal:
       the RAM cache is already correct, and a subsequent svc_config_set_*
       call will re-persist. */
    (void)persist_u8(NVS_KEY_METHOD,   (uint8_t)s_cache.method);
    (void)persist_u8(NVS_KEY_MADHAB,   (uint8_t)s_cache.madhab);
    (void)persist_u8(NVS_KEY_HIGH_LAT, s_cache.high_lat_rule);

    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        char key[24];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_ANCHOR_W, p);
        (void)persist_u8(key, s_cache.jamat[p].winter.anchor);
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_ANCHOR_S, p);
        (void)persist_u8(key, s_cache.jamat[p].summer.anchor);
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_OFF_W, p);
        (void)persist_u16(key, s_cache.jamat[p].winter.offset_min);
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_OFF_S, p);
        (void)persist_u16(key, s_cache.jamat[p].summer.offset_min);
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_JAMAT_ROUND, p);
        (void)persist_u8(key, s_cache.jamat[p].round_min);
        (void)persist_u16(k_iqamah_keys[p], s_cache.jamat[p].winter.offset_min);
    }
    (void)persist_u8(NVS_KEY_SEASON_SUMMER_M, s_cache.season.summer_start_month);
    (void)persist_u8(NVS_KEY_SEASON_SUMMER_D, s_cache.season.summer_start_day);
    (void)persist_u8(NVS_KEY_SEASON_WINTER_M, s_cache.season.winter_start_month);
    (void)persist_u8(NVS_KEY_SEASON_WINTER_D, s_cache.season.winter_start_day);

    lock_give();
    return ESP_OK;
}

esp_err_t svc_config_set_custom_angles(double fajr, double isha)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (fajr < 0.0 || fajr > 30.0 || isha < 0.0 || isha > 30.0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lock_take()) return ESP_ERR_TIMEOUT;

    s_cache.custom_fajr_angle = fajr;
    s_cache.custom_isha_angle = isha;

    int32_t f_milli = (int32_t)(fajr * 1000.0 + 0.5);
    int32_t i_milli = (int32_t)(isha * 1000.0 + 0.5);

    esp_err_t e1 = persist_i32(NVS_KEY_CUST_FAJR, f_milli);
    esp_err_t e2 = persist_i32(NVS_KEY_CUST_ISHA, i_milli);

    lock_give();
    if (e1 != ESP_OK) return e1;
    return e2;
}

