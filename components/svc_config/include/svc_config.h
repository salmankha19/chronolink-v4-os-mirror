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

/* Calculation method IDs. Values 0..7 match svc_prayer_method_t
   exactly. Moonsighting Committee (6) applies seasonal twilight bounds.
   Custom (7) uses user-provided Fajr/Isha angles. */
typedef enum {
    SVC_CONFIG_METHOD_MWL         = 0,
    SVC_CONFIG_METHOD_ISNA        = 1,
    SVC_CONFIG_METHOD_EGYPTIAN    = 2,
    SVC_CONFIG_METHOD_KARACHI     = 3,
    SVC_CONFIG_METHOD_UMM_AL_QURA = 4,
    SVC_CONFIG_METHOD_JAFARIA      = 5,  /* Shia Ithna-Ashari */
    SVC_CONFIG_METHOD_MOONSIGHTING = 6,
    SVC_CONFIG_METHOD_CUSTOM       = 7,  /* user-editable Fajr/Isha angles */
    SVC_CONFIG_METHOD_COUNT        = 8,
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

/* Quick Setup city presets. A preset seeds the fields that are a property
   of the *region*: location, calculation method, high-latitude rule, Asr
   madhab and the Hijri options. It deliberately does NOT set adhan margins
   or jamat times -- those belong to the mosque, not the city, and a preset
   must never overwrite a published timetable
   (docs/prayer-times-model.md section 5).

   The numeric order is both the JSON `preset` field of
   POST /api/config/preset and the option order of the Quick Setup
   dropdown in www/index.html. HORSHAM_UK is the factory default; see
   set_defaults() in svc_config.c. */
typedef enum {
    SVC_CONFIG_PRESET_HORSHAM_UK    = 0,
    SVC_CONFIG_PRESET_CRAWLEY_UK    = 1,
    SVC_CONFIG_PRESET_LONDON_UK     = 2,
    SVC_CONFIG_PRESET_LAHORE_PK     = 3,
    SVC_CONFIG_PRESET_BAHAWALPUR_PK = 4,
    SVC_CONFIG_PRESET_PENANG_MY     = 5,
    SVC_CONFIG_PRESET_JEDDAH_SA     = 6,
    SVC_CONFIG_PRESET_TORONTO_CA    = 7,
    SVC_CONFIG_PRESET_NEW_JERSEY_US = 8,
    SVC_CONFIG_PRESET_COUNT         = 9,
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
    double   custom_fajr_angle; /* Used only if method == CUSTOM */
    double   custom_isha_angle; /* Used only if method == CUSTOM */
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

    // User offset for the Hijri calendar, in days (-3 to +3).
    // Applied to the conversion to match local mosque announcements.
    int8_t   hijri_offset_days;

    // Calculation method for the Hijri date.
    // 0 = Tabular (Kuwaiti algorithm)
    // 1 = Umm al-Qura (KSA bitmask table) [DEFAULT]
    // Placed at the end of the struct to preserve existing field offsets.
    uint8_t  hijri_calc_method;
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

// True iff (year, month, day) is inside Ramadan for cfg, using the
// algorithmic Gregorian-to-Hijri conversion selected by
// cfg->hijri_calc_method (0 = Kuwaiti tabular, 1 = Umm al-Qura KSA).
//
// The conversion applies cfg->hijri_offset_days (-3 to +3) to align
// with local mosque announcements.
//
// NULL cfg returns false. No allocation, no lock, no NVS access -- the
// caller passes a snapshot from svc_config_get(). Safe to call from the
// prayer service worker, from svc_timetable regen, or anywhere else that
// already has a cfg snapshot.
bool svc_config_is_ramadan(const svc_config_t *cfg, int year, int month, int day);

// Hijri date for (year, month, day), using cfg's hijri_offset_days and
// hijri_calc_method. Returns true on success; false on any error (NULL
// cfg or output pointer, year outside [1900, 2100], month outside [1,12],
// day outside [1,31]). Fills all output pointers only on success. Both
// month-name buffers must be at least 16 bytes; they receive the full
// form ("Jumada al-Ula") and the short form ("Jumada I") respectively,
// NUL-terminated.
bool svc_config_get_hijri_date(const svc_config_t *cfg, int year, int month, int day,
                               int *out_year, int *out_month, int *out_day,
                               char *out_month_name_full,
                               char *out_month_name_short);

esp_err_t svc_config_set_location_name(const char *name);
esp_err_t svc_config_set_location(double lat, double lon);
esp_err_t svc_config_clear_location(void);
esp_err_t svc_config_set_method(svc_config_method_t m);

/* Set custom Fajr and Isha angles (0.0 to 30.0). Validates range. */
esp_err_t svc_config_set_custom_angles(double fajr, double isha);
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

/* Apply a city preset. Overwrites latitude, longitude, location_valid,
   method, madhab, high_lat_rule, hijri_calc_method and
   hijri_offset_days. location_name is seeded from the city name ONLY when
   it is currently empty, so an existing mosque name survives a preset
   change.

   Does NOT touch jamat[], season, adhan_mode, adhan_volume,
   brightness_mode, auto_brightness, night_lamp_enable, zawal_buffer_min,
   custom_fajr_angle, custom_isha_angle, or per-prayer adhan_offset_min[]:
   congregational times are mosque-specific and are configured separately.
   Persists the affected fields to NVS. */
esp_err_t svc_config_apply_preset(svc_config_preset_t preset);

/* The POSIX TZ string a preset implies. #132 (R4): svc_time owns the
   timezone (NVS namespace "time" key "tz"); this helper only maps a
   preset to the string svc_http should hand to
   svc_time_set_timezone_posix() after svc_config_apply_preset() runs.
   Returns NULL for any unrecognized preset, so a caller can skip the
   TZ application.

   Horsham / Crawley / London -> "GMT0BST,M3.5.0/1,M10.5.0" (GMT + BST).
   Lahore / Bahawalpur -> "PKT-5" (Pakistan Standard Time, UTC+5, no DST).
   Penang -> "MYT-8" (UTC+8, no DST).
   Jeddah -> "AST-3" (UTC+3, no DST).
   Toronto / New Jersey -> "EST5EDT,M3.2.0,M11.1.0" (US DST rules).
   POSIX TZ strings use inverted sign: -5 means 5 hours east of UTC.

   Note: set_defaults() cannot set the timezone (svc_time owns it), but
   svc_time's own factory default is already the UK string, so a fresh
   device boots on Horsham time correctly. */
const char *svc_config_preset_timezone(svc_config_preset_t preset);

esp_err_t svc_config_set_adhan_mode(svc_config_adhan_mode_t mode);
esp_err_t svc_config_set_adhan_volume(uint8_t volume_0_100);
esp_err_t svc_config_set_brightness_mode(uint8_t mode);
esp_err_t svc_config_set_high_lat_rule(uint8_t rule);
esp_err_t svc_config_set_adhan_offset(svc_config_prayer_t p, uint16_t minutes);
esp_err_t svc_config_set_zawal_buffer(uint16_t minutes);

// Set the user offset for the Hijri calendar.
// Valid range is -3 to +3 days. Persisted to NVS.
esp_err_t svc_config_set_hijri_offset(int8_t offset_days);

// Set the Hijri calendar calculation method.
// 0 = Tabular (Kuwaiti), 1 = Umm al-Qura (KSA). Persisted to NVS.
esp_err_t svc_config_set_hijri_method(uint8_t method);

esp_err_t svc_config_set_auto_brightness(bool enabled);
esp_err_t svc_config_set_night_lamp(bool enabled);

#ifdef __cplusplus
}
#endif
