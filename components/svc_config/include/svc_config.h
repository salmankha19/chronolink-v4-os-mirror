/*
 * svc_config.h
 *
 * ChronoLink V4 OS -- configuration service (public API)
 *
 * Owns the NVS namespace "config" and provides typed accessors for
 * installation settings, location, calculation parameters, and UI/audio
 * preferences. Does NOT calculate prayer times or drive hardware.
 *
 * Consumer status of the fields in this schema:
 *
 *   Live today (read by an existing consumer):
 *     location_name, latitude, longitude, location_valid,
 *     method, madhab, iqamah_offset_min[]
 *
 *   Stored only (no consumer yet -- persisted so the web UI can save
 *   them and a future component can honor them without a schema change):
 *     adhan_mode             -> svc_audio (Phase 3)
 *     adhan_volume           -> never on this PCB (fixed GAIN pin); future HW only
 *     brightness_mode        -> backlight PWM HAL (separate PR)
 *     auto_brightness        -> backlight PWM + VEML7700 polling (separate PR)
 *     night_lamp_enable      -> full-screen warm fill + low backlight (separate PR)
 *     high_lat_rule          -> svc_prayer_engine (PR 14)
 *     adhan_offset_min       -> svc_prayer_engine_service (PR 15)
 *     zawal_buffer_min       -> svc_prayer_engine_service (PR 15)
 *
 * The honest-stub rule (Constitution section 12) forbids returning success
 * from a function that does nothing. It does not forbid storing a value that
 * nothing reads yet. These fields genuinely persist, are genuinely read
 * back, and will genuinely be honored when their consumer lands.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Capacity of location_name including the terminating NUL. */
#define SVC_CONFIG_NAME_MAX 64

/* Calculation method IDs. Values 0..6 match svc_prayer_method_t
   exactly. Moonsighting Committee (6) applies seasonal twilight bounds
   in the engine (see docs/prayer-times-model.md §3.5). */
typedef enum {
    SVC_CONFIG_METHOD_MWL         = 0,
    SVC_CONFIG_METHOD_ISNA        = 1,
    SVC_CONFIG_METHOD_EGYPTIAN    = 2,
    SVC_CONFIG_METHOD_KARACHI     = 3,
    SVC_CONFIG_METHOD_UMM_AL_QURA = 4,
    SVC_CONFIG_METHOD_JAFARIA      = 5,  /* Shia Ithna-Ashari */
    SVC_CONFIG_METHOD_MOONSIGHTING = 6,
    SVC_CONFIG_METHOD_COUNT        = 7,
} svc_config_method_t;

typedef enum {
    SVC_CONFIG_MADHAB_STANDARD = 0,
    SVC_CONFIG_MADHAB_HANAFI   = 1,
    SVC_CONFIG_MADHAB_COUNT    = 2,
} svc_config_madhab_t;

typedef enum {
    SVC_CONFIG_PRAYER_FAJR    = 0,
    SVC_CONFIG_PRAYER_DHUHR   = 1,
    SVC_CONFIG_PRAYER_ASR     = 2,
    SVC_CONFIG_PRAYER_MAGHRIB = 3,
    SVC_CONFIG_PRAYER_ISHA    = 4,
    SVC_CONFIG_PRAYER_COUNT   = 5,
} svc_config_prayer_t;

typedef enum {
    SVC_CONFIG_ADHAN_OFF        = 0,   /* disabled */
    SVC_CONFIG_ADHAN_FULL       = 1,   /* full adhan */
    SVC_CONFIG_ADHAN_SHORT      = 2,   /* ~5-10 s sample only */
    SVC_CONFIG_ADHAN_MODE_COUNT = 3,
} svc_config_adhan_mode_t;

/* Jamat anchor: how the congregational time is derived from a base time. */
typedef enum {
    SVC_CONFIG_JAMAT_DISABLED       = 0,
    SVC_CONFIG_JAMAT_ADHAN_PLUS     = 1,  /* jamat = adhan + offset        */
    SVC_CONFIG_JAMAT_SUNRISE_MINUS  = 2,  /* Fajr only: jamat = sunrise - offset */
    SVC_CONFIG_JAMAT_MIDNIGHT_MINUS = 3,  /* Isha only: jamat = midnight - offset */
    SVC_CONFIG_JAMAT_ANCHOR_COUNT   = 4,
} svc_config_jamat_anchor_t;

/* One season's jamat rule for one prayer. */
typedef struct {
    uint8_t  anchor;       /* svc_config_jamat_anchor_t */
    uint16_t offset_min;
} jamat_season_cfg_t;

/* Both seasons + rounding for one prayer. Rounding is shared between
   seasons (it reflects how the mosque publishes its sign, not season). */
typedef struct {
    jamat_season_cfg_t winter;
    jamat_season_cfg_t summer;
    uint8_t round_min;     /* 0, 2, 5, 10, 15, 20, 30; 0 = no rounding */
} jamat_per_prayer_cfg_t;

/* Two fixed dates split the year: summer = [summer_start, winter_start),
   winter = the complement. Both dates are (month, day); year wraps. */
typedef struct {
    uint8_t summer_start_month;
    uint8_t summer_start_day;
    uint8_t winter_start_month;
    uint8_t winter_start_day;
} season_cfg_t;

typedef enum {
    SVC_CONFIG_PRESET_UK                = 0,
    SVC_CONFIG_PRESET_PAKISTAN          = 1,
    SVC_CONFIG_PRESET_UK_MOONSIGHTING   = 2,
    SVC_CONFIG_PRESET_COUNT             = 3,
} svc_config_preset_t;

/* Valid values for jamat_per_prayer_cfg_t.round_min. The setter rejects
   anything not on this list so the web UI dropdown and the API agree. */
bool svc_config_round_min_is_valid(uint8_t round_min);

/* Snapshot of all configuration fields. */
typedef struct {
    char     location_name[SVC_CONFIG_NAME_MAX]; /* "Masjid", "Home", etc. "" if unset */
    bool     location_valid;                     /* true iff lat/lon set */
    double   latitude;
    double   longitude;
    svc_config_method_t method;
    svc_config_madhab_t madhab;
    /* DEPRECATED legacy mirror of jamat[i].winter.offset_min. Populated
       automatically. Kept for backward compatibility with the pre-5.1a
       API (`svc_config_set_iqamah_offset`) and web UI field
       "iqamah_offsets". Removed in 5.1b when the UI is updated. */
    uint16_t iqamah_offset_min[SVC_CONFIG_PRAYER_COUNT];

    jamat_per_prayer_cfg_t jamat[SVC_CONFIG_PRAYER_COUNT];
    season_cfg_t           season;

    svc_config_adhan_mode_t adhan_mode;
    uint8_t  adhan_volume;          /* 0..100; no digital volume control on shipped PCB */
    uint8_t  brightness_mode;       /* 0=daylight, 1=evening, 2=calm */
    /* 0=auto (default), 1=none, 2=middle, 3=seventh, 4=angle.
       AUTO uses seventh-of-night > 48° latitude (UK/Europe) to avoid
       twilight collision, and middle-of-night <= 48° (Middle East/Asia).
       User can override at runtime. */
    uint8_t  high_lat_rule;
    uint16_t adhan_offset_min[SVC_CONFIG_PRAYER_COUNT];
    uint16_t zawal_buffer_min;
    bool     auto_brightness;
    bool     night_lamp_enable;
} svc_config_t;

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/*
 * Open the NVS namespace "config", load the cached snapshot, and create the
 * internal mutex. Idempotent.
 *
 * Non-fatal if NVS is unavailable: the service still initialises with
 * built-in defaults and returns ESP_OK. Setters will return
 * ESP_ERR_NVS_NOT_INITIALIZED in that case, but RAM updates still apply.
 *
 * Returns ESP_FAIL only if the mutex could not be created.
 */
esp_err_t svc_config_start(void);

/* Copy the current snapshot. Returns ESP_ERR_INVALID_ARG if out is NULL,
   ESP_ERR_INVALID_STATE if svc_config_start() has not run, or
   ESP_ERR_TIMEOUT if the service lock could not be taken within 1 s. */
esp_err_t svc_config_get(svc_config_t *out);

esp_err_t svc_config_set_location_name(const char *name);
esp_err_t svc_config_set_location(double lat, double lon);
esp_err_t svc_config_clear_location(void);
esp_err_t svc_config_set_method(svc_config_method_t m);
esp_err_t svc_config_set_madhab(svc_config_madhab_t m);
/* DEPRECATED. Writes to jamat[p].winter.offset_min AND
   jamat[p].summer.offset_min (both seasons), preserving pre-5.1a
   behaviour. Use svc_config_set_jamat_season() for per-season control. */
esp_err_t svc_config_set_iqamah_offset(svc_config_prayer_t p, uint16_t minutes);

/* Set one prayer's jamat rule for one season (0 = winter, 1 = summer).
   Validates anchor for the prayer: SUNRISE_MINUS only for FAJR,
   MIDNIGHT_MINUS only for ISHA. offset_min <= 1440. */
esp_err_t svc_config_set_jamat_season(svc_config_prayer_t p, uint8_t season,
                                      svc_config_jamat_anchor_t anchor,
                                      uint16_t offset_min);

/* Set one prayer's rounding. Rejects values not in the valid list. */
esp_err_t svc_config_set_jamat_round(svc_config_prayer_t p, uint8_t round_min);

/* Set the two season transition dates. Both must be valid (month 1-12,
   day 1-31). The two dates must differ. */
esp_err_t svc_config_set_season(uint8_t summer_m, uint8_t summer_d,
                                uint8_t winter_m, uint8_t winter_d);

/* Apply a named preset. Overwrites method, madhab, high_lat_rule,
   jamat[], season. Does NOT touch location_name, latitude, longitude,
   adhan_mode, adhan_volume, brightness_mode, auto_brightness,
   night_lamp_enable, zawal_buffer_min, or per-prayer adhan_offset_min.
   Persists the affected fields to NVS. */
esp_err_t svc_config_apply_preset(svc_config_preset_t preset);
esp_err_t svc_config_set_adhan_mode(svc_config_adhan_mode_t mode);
esp_err_t svc_config_set_adhan_volume(uint8_t volume_0_100);
esp_err_t svc_config_set_brightness_mode(uint8_t mode);
esp_err_t svc_config_set_high_lat_rule(uint8_t rule);
esp_err_t svc_config_set_adhan_offset(svc_config_prayer_t p, uint16_t minutes);
esp_err_t svc_config_set_zawal_buffer(uint16_t minutes);
esp_err_t svc_config_set_auto_brightness(bool enabled);
esp_err_t svc_config_set_night_lamp(bool enabled);

#ifdef __cplusplus
}
#endif
