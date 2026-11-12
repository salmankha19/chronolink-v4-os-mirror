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

// Maximum number of user Ramadan-window overrides. One per Hijri year is
// enough for any realistic lookahead; eight covers more than a decade of
// future planning and keeps svc_config_t well within the NVS blob budget
// (8 x 8 bytes = 64 bytes plus a count byte).
#define SVC_CONFIG_RAMADAN_MAX 8

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

// A single user-supplied Ramadan window: inclusive Gregorian date range
// (start_date .. end_date). end_year is explicit so a same-year window and
// a single-year-cross window (end_year == start_year + 1) are the only two
// accepted shapes, and validation is a plain date comparison rather than an
// end_month < start_month heuristic.
//
// Parallel to uaq_ramadan_window_t (now in components/svc_config/src/
// hijri_calendar.h) but NOT the same shape, and not reused on purpose:
//
//   - uaq_ramadan_window_t carries only start_year, and derives end_year
//     implicitly (end_year = start_year + 1 iff end_month < start_month).
//     That keeps the confirmed KSA table rows compact: every entry needs
//     only one year, and a cross-year window is expressed by month order.
//
//   - svc_config_ramadan_window_t carries explicit start_year AND
//     end_year, because the validator rejects multi-year windows with a
//     single comparison (end_year > start_year + 1). Deriving end_year
//     from a month-wrap heuristic would let a user persist a decade-long
//     window that silently suppresses non-Ramadan Isha year-round.
//
// Collapsing the two would force one of those to lose its natural shape:
// either every KSA table row must duplicate a year, or the multi-year
// guard disappears. The 10-byte cost of two distinct types is cheaper
// than either.
typedef struct {
    uint16_t start_year;
    uint8_t  start_month;
    uint8_t  start_day;
    uint16_t end_year;
    uint8_t  end_month;
    uint8_t  end_day;
} svc_config_ramadan_window_t;

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

    // User override list for Ramadan windows. Entries 0..count-1 are live;
    // entries beyond count are ignored (their content is unspecified).
    // count == 0 means "no overrides" -- the service falls back to the
    // confirmed KSA table in hijri_calendar.c.
    //
    // Placed at the end of the struct so the offsets of every existing field
    // are unchanged: the four fields above keep their current positions, so
    // any code that zero-initialises svc_config_t and sets specific fields
    // (all existing callers) sees no behavioural change.
    svc_config_ramadan_window_t ramadan_windows[SVC_CONFIG_RAMADAN_MAX];
    uint8_t  ramadan_window_count;
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

// True iff (year, month, day) is inside a Ramadan window for cfg.
//
// Resolution order (user override REPLACES the confirmed table for any
// year that has at least one override entry):
//
//   1. If any cfg->ramadan_windows[0..count-1] entry has start_year ==
//      year OR end_year == year, then the user owns that year: return
//      true iff (year, month, day) falls inside some override window,
//      and NEVER fall through to the confirmed table for that year.
//      A user who narrows the window (e.g. Feb 17 - Mar 18 vs the
//      table's Feb 18 - Mar 19) gets exactly their narrower window.
//
//   2. Else, no override touches year: use the confirmed KSA table via
//      uaq_is_ramadan(). If the table has no entry for year, returns
//      false (safe non-Ramadan, +90 min Isha for UMM_AL_QURA).
//
// NULL cfg returns false. No allocation, no lock, no NVS access -- the
// caller passes a snapshot from svc_config_get(). Safe to call from the
// prayer service worker, from svc_timetable regen, or anywhere else that
// already has a cfg snapshot.
bool svc_config_is_ramadan(const svc_config_t *cfg, int year, int month, int day);

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

/* Apply a named preset. Overwrites method, madhab, high_lat_rule,
   jamat[], season. Does NOT touch location_name, latitude, longitude,
   adhan_mode, adhan_volume, brightness_mode, auto_brightness,
   night_lamp_enable, zawal_buffer_min, or per-prayer adhan_offset_min.
   Persists the affected fields to NVS. */
esp_err_t svc_config_apply_preset(svc_config_preset_t preset);

/* The POSIX TZ string a preset implies. #132 (R4): svc_time owns the
   timezone (NVS namespace "time" key "tz"); this helper only maps a
   preset to the string svc_http should hand to
   svc_time_set_timezone_posix() after svc_config_apply_preset() runs.
   Returns NULL for any unrecognized preset, so a caller can skip the
   TZ application.

   UK and UK_MOONSIGHTING -> "GMT0BST,M3.5.0/1,M10.5.0" (GMT + BST).
   PAKISTAN -> "PKT-5" (Pakistan Standard Time, UTC+5, no DST).
   POSIX TZ strings use inverted sign: -5 means 5 hours east of UTC. */
const char *svc_config_preset_timezone(svc_config_preset_t preset);

esp_err_t svc_config_set_adhan_mode(svc_config_adhan_mode_t mode);
esp_err_t svc_config_set_adhan_volume(uint8_t volume_0_100);
esp_err_t svc_config_set_brightness_mode(uint8_t mode);
esp_err_t svc_config_set_high_lat_rule(uint8_t rule);
esp_err_t svc_config_set_adhan_offset(svc_config_prayer_t p, uint16_t minutes);
esp_err_t svc_config_set_zawal_buffer(uint16_t minutes);

// Replace the full user override list for Ramadan windows.
//
// Overrides take precedence over the confirmed KSA table
// (uaq_ramadan_confirmed_window) when the prayer service resolves
// is_ramadan for a date. Entries beyond count are ignored; count == 0
// clears all overrides. To clear, call with (NULL, 0).
//
// Validation: count must be <= SVC_CONFIG_RAMADAN_MAX. For count > 0,
// windows must be non-NULL and every entry must pass
// svc_config_ramadan_window_is_valid(). On invalid input the call returns
// ESP_ERR_INVALID_ARG and leaves the persisted value unchanged.
//
// On success the RAM cache is updated in all cases and the value is
// persisted to NVS when NVS is available. A failed persist returns
// ESP_ERR_NVS_NOT_INITIALIZED (RAM cache still updated), matching every
// other setter in this file.
esp_err_t svc_config_set_ramadan_windows(
    const svc_config_ramadan_window_t *windows,
    uint8_t count);

esp_err_t svc_config_set_auto_brightness(bool enabled);
esp_err_t svc_config_set_night_lamp(bool enabled);

#ifdef __cplusplus
}
#endif
