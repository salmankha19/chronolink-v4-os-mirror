# Prayer Times Model

The authoritative description of how prayer times, adhan times, and
jamat times are modelled, computed, and exposed in ChronoLink V4 OS.

**Status:** design complete (2026-10-25); implemented in PR 5.1a
(`svc_config`, `svc_prayer_engine_service`, `svc_http`, web UI).
Where this document and the code disagree, the code wins — fix the document.

**Related documents:** [Architecture.md](Architecture.md) §4 (runtime
model), [STATUS.md](STATUS.md) (implementation status),
[CHRONOLINK_CONSTITUTION.md](CHRONOLINK_CONSTITUTION.md) (layer rules).

---

## 1. Scope

Covers:

- the five time concepts (astronomical, adhan, jamat, auxiliary, Ramadan)
- the seasonal jamat model (anchors, offsets, rounding)
- the season split (two fixed dates)
- named presets (UK, Pakistan)
- tomorrow's jamat (for change-notification)
- the layer responsibilities and the published API shape
- the pre-5.1a migration path

Does **not** cover:

- the astronomical algorithm itself — see
  `components/svc_prayer_engine/include/prayertimes/PrayerTimes.hpp` and
  `src/prayertimes/PrayerTimes.cpp`
- UI layout decisions (ticker vs colour vs note)
- (Nothing. All planned calculation methods and custom angles are implemented.)

---

## 2. The five time concepts

Every prayer instant exists in one of five forms. Each form has a single
owner and a single published field.

| Concept | Definition | Computed by | Published as |
|---|---|---|---|
| **Astronomical** | Astronomical calculation (sun angle / declination / equation of time) + longitude correction + timezone offset, expressed as local civil time. **No user-configurable offsets.** | `PrayerTimesCalculator::calculate()` in the engine | `astronomical_*_sec` |
| **Adhan** | Astronomical + `adhan_offset_min[]` (safety margin). What the mosque actually calls. | `svc_prayer_engine_service` | `fajr_sec`, `dhuhr_sec`, etc. |
| **Jamat** | Adhan (or sunrise, or Islamic midnight) + seasonal offset + per-prayer rounding. What the mosque prays together. | `svc_prayer_engine_service` | `jamat_sec[]` |
| **Auxiliary** | Astronomical events, not prayers: sunrise, sunset, Islamic midnight, zawal start, asr yellowing. **No adhan_offset.** | Engine → passthrough | `sunrise_sec`, `sunset_sec`, `midnight_sec`, `zawal_start_sec`, `asr_yellow_sec` |
| **Ramadan** | Endpoints of fasting windows: Sehri ends at astronomical Fajr; Iftar at astronomical sunset. **No adhan_offset.** | Engine → aliased | `sehri_end_sec`, `iftar_sec` |

Why astronomical is separate from adhan: the prayer instants from the
astronomy and the times the mosque calls the adhan may differ (safety
margin, tradition, avoidance of disputes). Showing both lets the user
verify the calculation independently of the offset configuration. The
web UI displays adhan as primary and astronomical as a secondary "astro"
annotation.

Why Sehri ends at astronomical Fajr, not at adhan: if a mosque calls
Fajr adhan 5 minutes after the astronomical Fajr for safety, Sehri still
ends at the true Fajr — the fast begins at the astronomy, not at the call
to prayer. Same for Iftar: it breaks at sunset, not at the Jafaria
maghrib angle (which is 15+ minutes later in summer).

### 2.1 Minute rounding (PR 5.4)

`hours_to_sec()` in `svc_prayer_engine.cpp` converts the engine's decimal
hours to seconds. As of PR 5.4 it rounds **up** to the next whole minute
(ceiling) before any offsets are applied: Fajr 05:06:15 becomes 05:07:00,
never 05:06:00. This guarantees a published time is never earlier than
the astronomical instant. Adhan offsets (`adhan_offset_min[]`) and jamat
anchors/offsets then operate on minute-aligned values, so every published
`*_sec` field is a multiple of 60, with two documented exceptions:

- `asr_yellow_sec` may hold `UINT32_MAX` (the "sun never reached 5°"
  sentinel), which is not a multiple of 60.
- `isha_sec` / `midnight_sec` are clamped at 172799 (2 days − 1 s) as a
  safety net; that clamp value is not a multiple of 60 either, though it
  is unreachable in normal operation.

The sub-minute astronomical accuracy described in `STATUS.md` is
unchanged; only the published resolution is whole minutes.

---

## 3. Jamat model

Jamat is the most variable concept across mosques. The model exposes three
knobs, each per prayer:

### 3.1 Anchor (per prayer, per season)

How the jamat time is derived from a base time:

| Anchor | Formula | Validation |
|---|---|---|
| `SVC_CONFIG_JAMAT_DISABLED` | (no jamat this season) | any prayer |
| `SVC_CONFIG_JAMAT_ADHAN_PLUS` | jamat = adhan + `offset_min` | any prayer |
| `SVC_CONFIG_JAMAT_SUNRISE_MINUS` | jamat = sunrise − `offset_min` | **Fajr only** |
| `SVC_CONFIG_JAMAT_MIDNIGHT_MINUS` | jamat = midnight − `offset_min` | **Isha only** |

The sunrise/midnight anchors exist because some mosques set the
congregation time backwards from a hard astronomical boundary rather
than forwards from the adhan. UK Fajr is the canonical example: the
mosque sets jamat 45 minutes before sunrise, so winter jamat might be
07:15 while summer Fajr adhan is 03:45 (jamat disabled entirely).

`SUNRISE_MINUS` uses the **astronomical sunrise**, not the adhan-derived
one — a 5-minute safety margin on Fajr adhan moves the adhan, not the
sunrise anchor. If the user wants jamat to also move with the safety
margin, they increase `offset_min`.

### 3.2 Offset (per prayer, per season)

`offset_min` is a `uint16_t`, 0..1440. Meaning depends on the anchor:

- `ADHAN_PLUS`: minutes **after** adhan
- `SUNRISE_MINUS`: minutes **before** sunrise
- `MIDNIGHT_MINUS`: minutes **before** Islamic midnight

A mosque that changes its offset seasonally (UK Dhuhr: +25 in BST, +70
in GMT to keep jamat at ~13:30 year-round) expresses that as two
offsets — one winter, one summer — no DST detection needed.

### 3.3 Rounding (per prayer, shared across seasons)

`round_min` is a `uint8_t`, restricted to `{0, 2, 5, 10, 15, 20, 30}`.
Rounding is **shared between seasons** because it reflects how the
mosque publishes its sign ("Asr jamat is always at :30 or :35"), not
season.

`jamat = round(target, round_min)` where `target` is the anchor-derived
raw value. Rounding is to the nearest N-minute boundary from local
midnight.

- `round_min = 0`: no rounding, exact target
- `round_min = 5`: transitions once every ~5-10 days as the astronomical
  drift crosses a half-step
- `round_min = 30`: only changes when the target crosses :15/:45

**Defaults:** `5` for Fajr, Dhuhr, Asr, Isha; `2` for Maghrib
(iftar-critical, changes almost daily anyway).

The rounding mechanism is what makes jamat "stable" for the user without
storing any state. The transition from 3:45 to 3:50 happens naturally
when the raw target crosses 3:47:30 — the same behaviour a stateful
hysteresis implementation would produce, without any of the persisted
state.

### 3.4 Worked examples

**UK Fajr (winter):** sunrise 08:00, anchor `SUNRISE_MINUS`, offset
45, round 5 → target 07:15 → jamat **07:15**.

**UK Fajr (summer):** sunrise 04:30, anchor `DISABLED` → jamat **none**
(sunrise too early for a congregational Fajr).

**UK Dhuhr (winter):** adhan 12:05 GMT, anchor `ADHAN_PLUS`, offset 70,
round 5 → target 13:15 → jamat **13:15**.

**UK Dhuhr (summer):** adhan 13:05 BST, anchor `ADHAN_PLUS`, offset 25,
round 5 → target 13:30 → jamat **13:30**.

**Pakistan Dhuhr (any season):** adhan varies 12:15–12:45, anchor
`ADHAN_PLUS`, offset 70, round 5 → target 13:25–13:55, jamat lands at
**13:25 or 13:30** most days. The "fixed 13:30" effect emerges from the
offset + rounding combination, without a separate "absolute clock" mode.

**Asr drift illustration:** day 1 Asr adhan 15:30, offset 15, round 5 →
target 15:45 → jamat **15:45**. Day 4 Asr adhan 15:32 → target 15:47 →
jamat **15:45** (still). Day 8 Asr adhan 15:35 → target 15:50 → jamat
**15:50** (changed). The jamat time "sticks" for ~7 days, then steps.

### 3.5 Moonsighting Committee method

The Moonsighting Committee method (Khalid Shaukat, common in the UK and
North America) uses 18° for both Fajr and Isha, but applies a seasonal
adjustment to prevent Fajr from being too early and Isha too late at high
latitudes. The firmware ports the exact `batoulapps/adhan-js` algorithm:
a piecewise-linear interpolation across six year segments, giving a "safe"
boundary of N minutes before sunrise (Fajr) and M minutes after sunset
(Isha). The angle-based value is then clamped to this boundary: if the
angle would put Fajr before the safeFajr bound (or the angle is
unreachable / NaN), safeFajr is used. Symmetric for Isha.

The seasonal table replaces the high-latitude rule's function for this
method: angle NaN → safe bound; angle too early/late → safe bound; angle
within bounds → angle wins.

**Important interaction with High-Latitude Rule:** The AUTO high-latitude
rule runs *first*. At latitudes >48° (like the UK), AUTO resolves to
SEVENTH_OF_THE_NIGHT. In summer (e.g. June at 51°N), the seventh-of-night
bound is *tighter* (closer to sunrise) than the Moonsighting seasonal bound.
Because the seasonal bound only clamps if it is *earlier* than the current
Fajr, the seventh-of-night bound wins in summer.

For exact `adhan-js` / Pillar app parity, the user must explicitly set
**High Latitude Rule = None** in the web UI, allowing the Moonsighting
seasonal table to run unhindered.

---

## 4. Season model

Two fixed dates split the year:

```
summer = [summer_start, winter_start)   // year-wrapping
winter = the complement
```

`svc_config_t.season` holds `summer_start_{month,day}` and
`winter_start_{month,day}`. **UK default:** summer starts March 25,
winter starts October 25.

### 4.1 Why fixed dates, not DST detection

- **Portability.** Non-DST regions (Pakistan, Saudi, Gulf) can't use DST
  anyway. Fixed dates work everywhere.
- **Matches mosque practice.** Mosques typically publish a seasonal
  change notice tied to a round date ("from April 1") or the start of an
  Islamic month, not the exact DST hour.
- **No service coupling.** DST detection would need `svc_time` to expose
  "is DST active", adding a service dependency. Fixed dates keep
  `svc_config` self-contained.
- **Good enough.** The default dates are within a week of DST for the
  UK. Users adjust if their mosque changes on a different date.

If real demand emerges for DST-driven season detection, it can be added
as a fourth `season_mode` value without breaking existing config.

### 4.2 DST independence

The season split and the DST transition are **orthogonal**:

- Season affects **which jamat offset** is applied (winter vs summer).
- DST affects **what local civil time** the astronomical calculation
  maps to (the `tz_offset_minutes` parameter to the engine).

A device whose jamat config says `winter offset 70` will use 70 minutes
from adhan whether that adhan is expressed in GMT or BST — the offset
is relative, not absolute. So on DST transition day, both the adhan and
the jamat shift by an hour, together, exactly as the mosque expects.

---

## 5. Presets

A preset is a **bundle of configuration values** that seeds a fresh
device (or reconfigures an existing one) to a known-good baseline for a
region. Presets live in `svc_config.c`, applied via
`svc_config_apply_preset()`.

| Preset | Method | Madhab | Notes |
|---|---|---|---|
| `SVC_CONFIG_PRESET_UK` (factory default) | MWL | Standard | Fajr `SUNRISE_MINUS` 45 winter / disabled summer. Dhuhr +70 / +25. |
| `SVC_CONFIG_PRESET_PAKISTAN` | Karachi | Hanafi | Fajr +20 both seasons. Dhuhr +70 both. |
| `SVC_CONFIG_PRESET_UK_MOONSIGHTING` | Moonsighting | Standard | Same jamat rules as UK, but Moonsighting method (adhan-js parity). |

**A preset touches:** `method`, `madhab`, `high_lat_rule`, `jamat[]`,
`season`. **A preset does not touch:** `location_name`, `latitude`,
`longitude`, `adhan_mode`, `adhan_volume`, `brightness_mode`,
`auto_brightness`, `night_lamp_enable`, `zawal_buffer_min`, or the
per-prayer `adhan_offset_min[]`. Location data and display preferences
survive a preset change.

Presets are not templates in the `Method.hpp` sense. `Method` captures
*which algorithm to run* (angles, intervals). A preset captures
*reasonable starting values for a region* across several unrelated
fields. There is no preset registry in the engine — presets are pure
data in `svc_config.c`.

---

## 5.5 Year-calculated timetable (PR 5.5)

The device generates a full-year timetable to `/storage/timetable.json`
once per year (or on any prayer-config change), and looks up each day
from the file instead of calling the engine twice per recalculate. The
primary motivation is **jamat**: the congregational time is a mosque
decision with no universal formula, and a mosque's own published
timetable must be honoured verbatim.

**File format** (both firmware-generated and user-uploaded): final
local-time seconds. No astronomical times, no offsets.

**Firmware-generated file** fills every field (`jamat_included: true`).
**User-uploaded file** may omit any field per day — missing fields are
computed from config on **read**, never on upload. This means a config
change after upload immediately affects the displayed times for missing
fields; there is no silent staleness.

**Regeneration** (firmware files only): file missing, unparseable, year
mismatch, or `calculation` block ≠ current `svc_config_t` (canonical
JSON compare). User files never auto-regenerate (handoff dec. 7).

**Tripwire**: every recalculate where the file supplied today's values
compares against a fresh engine call; a >60 s mismatch logs a warning and
serves the fresh value for that field. Never fires in normal operation
(deterministic inputs).

**Year guard**: prayer times are never published when `current_year ∉
[2020, 2100]`.

**Jamat bracket validation**: file jamat < adhan → reject, fall back to
computed. File jamat ≥ next prayer → accept with warn (mosque authority
wins).

**Config-change warning**: if a partial user file is present and the
user saves config, the server returns 409 unless the client sends
`X-Confirm-Timetable-Override`; the web UI shows a `confirm()` before
that POST.

---

## 6. Tomorrow's jamat

`svc_prayer_engine_times_t.jamat_tomorrow_sec[]` publishes tomorrow's
jamat for each prayer, computed with:

- tomorrow's **date** (for the astronomical calculation)
- tomorrow's **season** (winter/summer, from `current_season()`)
- tomorrow's **timezone offset** (from
  `tz_offset_minutes_at_noon(tomorrow)`)

This makes the pair (`jamat_sec[p]`, `jamat_tomorrow_sec[p]`)
**always consistent**: on any day N, tomorrow's value is exactly what
day N+1 will publish as `jamat_sec[p]` when midnight rolls.

Consumers compare the two to detect a change:

```c
if (times.jamat_sec[p] != times.jamat_tomorrow_sec[p]) {
    // UI: show "→ tomorrow HH:MM" or equivalent notification
}
```

The `UINT32_MAX` sentinel in either field means "no jamat for that
prayer / that day" — treat as no-change rather than a transition.

### 6.1 What triggers the notification

The notification is purely **consumer-side**. `svc_prayer_engine_service`
does not fire a separate event. Consumers react to the existing
`EVENT_PRAYER_TIMES_UPDATED` (fires at 00:01 daily, on NTP sync, on
config change) and re-read the snapshot. This keeps the notification
logic in the UI layer where layout decisions belong, and gives future
consumers (LCD ticker, app push notification) a single API to build on.

### 6.2 Not done (deliberately)

- **Multi-day lookahead** (2+ days ahead). Requires N engine calls per
  recalculate. Deferred until the LCD actually wants it.
- **Dedicated event.** `EVENT_PRAYER_TIMES_UPDATED` already carries
  enough to signal; a separate event adds API surface for zero benefit.

---

## 7. Recalculation triggers

`svc_prayer_engine_service_recalculate()` runs on:

| Trigger | Source |
|---|---|
| Boot | `svc_prayer_engine_service_start()` |
| Successful NTP sync | `EVENT_TIME_UPDATE` |
| Date or UTC-offset change | `EVENT_TIME_DATE_CHANGED` (pure date rollover defers to 00:01) |
| Config change via web UI | `post_config_handler()` calls `_recalculate()` |
| Preset applied | `post_preset_handler()` calls `_recalculate()` |
| 60 s self-heal | Worker task timeout (in case an event is dropped) |

Each trigger recomputes the entire snapshot including tomorrow's jamat.
The cache key is (`date_yyyymmdd`, `tz_offset_minutes`); unchanged keys
skip the recalculation unless forced.

---

## 8. Layer responsibilities

| Layer | Owns | Does **not** own |
|---|---|---|
| `PrayerTimes.cpp` (engine) | Astronomical math. Returns local civil time for one date. | User offsets, adhan offsets, jamat, rounding, seasons |
| `svc_prayer_engine_service` | Applies adhan offsets, computes jamat (seasonal, with rounding), publishes snapshot including tomorrow | Astronomical math, config persistence |
| `svc_config` | Stores config, applies presets, persists NVS, validates ranges | Calculation, jamat computation |
| `svc_http` | Serializes snapshot to nested JSON | Any arithmetic |
| Web UI | Formats seconds for display, shows change hints | Any arithmetic (no `+ offset`, no rounding) |

The UI reads **finished values** and applies no math. This is the
consumer contract.

### 8.1 Planned refactor (PR 5.1a.1)

After 5.1a lands, `svc_prayer_engine_service.c` is split into two internal
modules without API change:

- `adhan_calculator.{c,h}` — wraps engine + adhan_offset application
- `jamat_calculator.{c,h}` — pure arithmetic, no engine call

The published snapshot struct is unchanged. Future changes (Moonsighting
method, multi-session jamat, Jumu'ah overrides) touch one module each
rather than the monolithic `recalculate()`.

---

## 9. Configuration schema (post-5.1a)

```c
// svc_config.h additions
typedef struct {
    uint8_t  anchor;       // svc_config_jamat_anchor_t
    uint16_t offset_min;
} jamat_season_cfg_t;

typedef struct {
    jamat_season_cfg_t winter;
    jamat_season_cfg_t summer;
    uint8_t round_min;     // 0, 2, 5, 10, 15, 20, 30
} jamat_per_prayer_cfg_t;

typedef struct {
    uint8_t summer_start_month, summer_start_day;
    uint8_t winter_start_month, winter_start_day;
} season_cfg_t;

// svc_config_t new fields
jamat_per_prayer_cfg_t jamat[SVC_CONFIG_PRAYER_COUNT];
season_cfg_t           season;
```

### 9.1 NVS keys

| Key pattern | Type | Meaning |
|---|---|---|
| `jamat_anc_w_<p>` | u8 | winter anchor for prayer p (0..4) |
| `jamat_anc_s_<p>` | u8 | summer anchor |
| `jamat_off_w_<p>` | u16 | winter offset (minutes) |
| `jamat_off_s_<p>` | u16 | summer offset |
| `jamat_rnd_<p>` | u8 | rounding (must be in valid list) |
| `season_sum_m`, `season_sum_d` | u8 | summer start |
| `season_win_m`, `season_win_d` | u8 | winter start |

### 9.2 Migration from pre-5.1a

Pre-5.1a `iqamah_offset_min[i]` (NVS keys `iqamah_fajr`..) is loaded as:

- `jamat[i].winter.offset_min = jamat[i].summer.offset_min = old value`
- `anchor = ADHAN_PLUS`, `round_min = 5` (Maghrib: 2)
- Season defaults to UK (Mar 25 / Oct 25)

Old NVS keys stay readable for firmware downgrade (never erased by 5.1a).

`svc_config_set_iqamah_offset()` continues to work — it writes the same
offset to both seasons, preserving pre-5.1a behaviour. Removal is a
5.1c follow-up (`refactor(config): drop iqamah mirror`).

---

## 10. Published API

### 10.1 `GET /api/prayers`

Nested three-tier shape plus flat aliases for backward compatibility:

```json
{
  "valid": true,
  "season": 0,
  "astronomical": {
    "fajr_sec": 19920, "dhuhr_sec": 46800, "asr_sec": 57300,
    "maghrib_sec": 63900, "isha_sec": 70800
  },
  "adhan": {
    "fajr_sec": 19920, "dhuhr_sec": 47100, "asr_sec": 58200,
    "maghrib_sec": 64080, "isha_sec": 71700
  },
  "jamat": {
    "today_sec":    [19920, 47100, 58500, 64080, 71700],
    "tomorrow_sec": [20100, 47100, 58500, 64080, 71700]
  },
  "auxiliary": {
    "sunrise_sec": 23400, "sunset_sec": 63900,
    "midnight_sec": 89100, "zawal_start_sec": 46500,
    "asr_yellow_sec": 63000
  },
  "ramadan": { "sehri_end_sec": 19920, "iftar_sec": 63900 },

  // flat aliases (pre-5.1a compatibility):
  "fajr_sec": 19920, "dhuhr_sec": 47100, "asr_sec": 58200,
  "maghrib_sec": 64080, "isha_sec": 71700,
  "jamat_sec": [19920, 47100, 58500, 64080, 71700],
  "jamat_tomorrow_sec": [20100, 47100, 58500, 64080, 71700]
}
```

All times are seconds since local midnight of the target date. Sentinel
`null` in any jamat entry means "no jamat" (anchor `DISABLED` or
computation failed for that date).

### 10.2 `GET /api/config`

Adds `jamat[]` (array of five objects with `winter_anchor`,
`winter_offset`, `summer_anchor`, `summer_offset`, `round_min`) and
`season` object. Existing `iqamah_offsets[]` and `adhan_offsets[]`
remain, backed by the deprecated mirror and the unchanged adhan field.

### 10.3 `POST /api/config/preset`

Body `{"preset": 0}` (UK, MWL), `{"preset": 1}` (Pakistan, Karachi), or
`{"preset": 2}` (UK Moonsighting). Applies preset, triggers immediate
recalculation, returns `{"status":"ok"}`.

---

## 11. Out of scope (documented to prevent re-litigation)

These are acknowledged as useful but deliberately excluded from 5.1a.
Each is straightforward to add later without changing 5.1a's schema
(stated cost refers to extra design, not implementation difficulty):

| Feature | Cost to add later | Notes |
|---|---|---|
| **Jumu'ah override** (Friday Dhuhr different from weekday) | Small | Add `jumuah_cfg` to `jamat_per_prayer_cfg_t`; Dhuhr only; service picks based on `tm_wday == 5` |
| **Multi-session jamat** (1st 13:30, 2nd 14:15) | Medium | `jamat_sec[]` becomes `jamat_sec[][N]`; API changes shape; consumers update |
| **Ramadan-specific overrides** (Isha shifted, Fajr not held) | Small | Add `ramadan_override` to `jamat_per_prayer_cfg_t`; service checks Hijri month |
| **Weekday vs weekend jamat** | Small-medium | Weekday bitmask per prayer per season |
| **Hysteresis** (distinct from rounding) | Small-medium | Needs persistent state; rounding mechanism usually suffices |
| **Multi-day lookahead** (2+ days) | Small | One more `svc_prayer_engine_calculate()` per day; recompute cost grows |
| **Third season** (Ramadan as a season) | Medium | Season becomes array; NVS migration |

None of these are in 5.1a. Each has an entry here so a future PR can
reference this document rather than re-deriving the model.

---

## 12. PR sequence

| PR | Scope | Status |
|---|---|---|
| **5.1a** | Backend: seasonal jamat, three-tier snapshot, today+tomorrow, `/api/config/preset` | Implemented in PR 5.1a |
| **5.1a.1** | `refactor:` extract `adhan_calculator` + `jamat_calculator` | After 5.1a merges |
| **5.1b** | Editable web UI (per-prayer winter/summer columns, season date pickers, preset dropdown) | Implemented in this PR |
| **5.2** | Moonsighting Committee method (port adhan-js seasonal tables) | Implemented in this PR |
| **5.3** | `SVC_PRAYER_METHOD_CUSTOM` with user-editable Fajr/Isha angles | Implemented in this PR |

---

## 13. Living document policy

This document describes the **model**. When the model changes:

1. Update this document first.
2. Then update the code that implements it.
3. Update `STATUS.md` in the same change.

If a rule below disagrees with the code, the code wins — report the
discrepancy and fix the document. Do not leave the disagreement in
place.

---

*End of document.*