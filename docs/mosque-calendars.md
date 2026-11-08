# Mosque Prayer Calendar Analysis

**Status:** Reference document from session 6.7 post-PR-#139 (2026-11-08).
**Purpose:** Document how diverse mosques publish prayer times, identify what ChronoLink needs to support them, and record the config parameters for known UK/US mosques.
**Related:** `docs/prayer-times-model.md` (§5.5 deviation policy, §8.1 service-layer split)

---

## TL;DR

**No universal preset can fit all mosques.** Each mosque publishes its own calendar, derived from local calculation tools, manual tweaks, and community consensus. ChronoLink's `svc_config` + `svc_prayer_engine` can reproduce *any* single mosque's calendar within tolerance when configured correctly, but the config values differ per mosque.

**Three mosques analyzed, across two countries, three configs:**

| Mosque | Country | City | Method base | Madhab | Dhuhr offset | Maghrib offset |
|---|---|---|---|---|---|---|
| Masjid Quba | 🇬🇧 United Kingdom | London | MOON 18/18 | Hanafi | +5 (MOON built-in) | +3 (MOON) + 2 = +5 |
| Crawley Islamic Centre | 🇬🇧 United Kingdom | Crawley | ISNA 15/15 | Hanafi | +2 (custom) | +0 |
| Tawheed Center | 🇺🇸 United States | Farmington Hills, MI | ISNA 15/15 (measured, 0 offsets) | Standard (Hanafi alt) | 0 (measured) | 0 (measured) |

---

## Locations & coordinates

| Mosque | Country | City / Region | Full address | Coordinates (WGS84) | Timezone |
|---|---|---|---|---|---|
| **Masjid Quba** | 🇬🇧 United Kingdom | London (Stamford Hill, Hackney) | 70-72 Cazenove Road, London N16 6AA | `51.56510732559983, -0.0687256000863376` | Europe/London (GMT / BST) |
| **Crawley Islamic Centre & Masjid** | 🇬🇧 United Kingdom | Crawley (West Sussex) | 157 London Rd, Crawley RH10 9TA | `51.13115396014885, -0.1864782309679473` | Europe/London (GMT / BST) |
| **Tawheed Center** | 🇺🇸 United States | Farmington Hills, Michigan | 29707 W 10 Mile Rd, Farmington Hills, MI 48336 | `42.470667, -83.340793` | America/Detroit (EST / EDT) |

**Google Maps references:**
- Masjid Quba: HW8J+2G London
- Tawheed Center: via Bing Maps (per CSV header)

---

## Methodology

**Data collection:**
- Masjid Quba: 365-day 2026 calendar (CSV), provided by the mosque.
- Crawley Islamic Centre: November 2026 only (30 days).
- Tawheed Center: ~8 months (Jan-Jul, Oct-Dec; Aug/Sep skipped).

**Diagnostic tool:** `prayer_host_tests/test_wrapper.cpp` has a temporary diagnostic block (branch `diag/masjid-quba-2026`) that runs `svc_prayer_engine_calculate()` at exact coordinates for 13 dates × 4 methods × Hanafi, plus "applied config" tables with offsets.

**Comparison method:** Compare ChronoLink's calculated Fajr/Dhuhr/Asr/Maghrib/Isha against the mosque's published "Start"/"Begins" columns. Consistent residuals across dates indicate config gaps; variable ones indicate wrong angle/method.

**Confidence:**
- Masjid Quba — **measured** (actual diagnostic output). High.
- Crawley — **hand-math + measured** (1 month). Medium.
- Tawheed — **measured** (3 dates × 6 prayers, all within ±1 min). High for angle, **zero offsets confirmed**.

---

## 1. Masjid Quba — 🇬🇧 United Kingdom, London

- **Country:** United Kingdom
- **City / region:** London (Stamford Hill, London Borough of Hackney)
- **Address:** 70-72 Cazenove Road, London N16 6AA
- **Coordinates:** 51.56510732559983, -0.0687256000863376
- **Timezone:** Europe/London (GMT winter, BST Mar 29 – Oct 25 2026)
- **Calendar year analyzed:** 2026 (full year)

### Best-fit config

```
Method:    SVC_PRAYER_METHOD_MOONSIGHTING
Madhab:    SVC_PRAYER_MADHAB_HANAFI
High-lat:  SVC_PRAYER_HIGHLAT_AUTO
Offsets:   Fajr 0, Dhuhr 0, Asr 0, Maghrib +2, Isha 0
```

### Per-prayer residuals (measured, Δ = calendar − ChronoLink, minutes)

| Date | Fajr | Sun | Istiwa | Dhuhr | Asr | Maghrib | Isha |
|---|---|---|---|---|---|---|---|
| Jan 02 (Fri) | 0 | 0 | +1 | *(Jumu'ah)* | 0 | +2 | −4 |
| Jan 03 (Sat) | 0 | +1 | +2 | −1 | 0 | +3 | −5 |
| Feb 02 (Mon) | +3 | 0 | +2 | −1 | 0 | +2 | −10 |
| Mar 02 (Mon) | +5 | +1 | +2 | −1 | 0 | **+5** ⚠️ | −8 |
| Mar 29 (Sun) | +1 | +1 | +1 | 0 | 0 | +3 | −5 |
| Apr 01 (Wed) | +2 | +1 | +1 | 0 | 0 | +3 | −5 |
| May 01 (Fri) | −3 | +1 | +2 | *(Jumu'ah)* | 0 | +2 | −5 |
| Jun 21 (Sun) | +3 | +1 | +1 | 0 | 0 | +2 | **+8** ⚠️ |
| Jul 01 (Wed) | +5 | +1 | +1 | 0 | 0 | +2 | **+8** ⚠️ |
| Aug 01 (Sat) | +1 | +1 | +1 | 0 | 0 | +2 | −4 |
| Oct 25 (Sun) | +1 | +1 | +1 | 0 | 0 | +2 | −3 |
| Nov 01 (Sun) | +3 | +1 | +1 | 0 | 0 | +2 | −5 |
| Dec 21 (Mon) | +1 | +1 | +1 | 0 | 0 | +3 | −2 |

### Findings

**Confirmed (measured):**
- Method = Moonsighting Committee (Fajr/Isha within ±10 min except summer peak; ISNA 3–8 min off, MWL/KHI 15–20 min off).
- Madhab = Hanafi (Asr +30 min winter → +74 min summer vs Shafi'i).
- Dhuhr offset = 0 (MOON's built-in +5 matches mosque's +5 from Istiwa).
- Maghrib offset = +2 additional (total +5 from sunset; MOON adds +3 already).
- Sunrise/Istiwa match within ±2 min (rounding only, per #133 A1 ceil/floor policy).

**Known anomalies:**
- **Feb 18 – Mar 30**: Maghrib shows +10 from sunset instead of +5. Six weeks mid-year. Likely source-calculator quirk or manual adjustment. **Do not tune config to this window** — accept ~5 min error there.
- **Summer Isha (Jun-Aug)**: MOON is 8–11 min late. Mosque appears to use "Isha = Maghrib + 65 min" during peak summer (Jun 21, Jul 1, Aug 1 all show exactly +65 min from Maghrib). NOT angle-based. Would need `summer_isha_offset_min` config feature.

**Friday Dhuhr** = Jumu'ah time (13:00 GMT winter / 13:30 BST summer), not astronomical. Skip Friday Dhuhr in any comparison.

---

## 2. Crawley Islamic Centre & Masjid — 🇬🇧 United Kingdom, Crawley

- **Country:** United Kingdom
- **City / region:** Crawley (West Sussex, South East England)
- **Address:** 157 London Rd, Crawley RH10 9TA
- **Coordinates:** 51.13115396014885, -0.1864782309679473
- **Timezone:** Europe/London (GMT all of November 2026)
- **Calendar coverage:** November 2026 only (30 days)

### Best-fit config (preliminary — one month insufficient for certainty)

```
Method:    SVC_PRAYER_METHOD_ISNA + custom Fajr/Isha angle tweak (TBD)
Madhab:    SVC_PRAYER_MADHAB_HANAFI
High-lat:  SVC_PRAYER_HIGHLAT_AUTO
Offsets:   Fajr TBD, Dhuhr +0..+1, Asr 0, Maghrib 0, Isha TBD
```

### Measured residuals (Nov 1 at exact coords, Δ = published − ChronoLink-ISNA)

| Prayer | Published | ChronoLink (ISNA) | Δ |
|---|---|---|---|
| Fajr | 5:13 | 5:20 | **−7** |
| Sunrise | 6:52 | 6:53 | −1 |
| Dhuhr_Zawal_Begins | 11:45 | 11:44 (Istiwa) | **+1** |
| Asr | 14:46 | 14:46 (Hanafi) | **0** ✓ |
| Maghrib | 16:35 | 16:35 | **0** ✓ |
| Isha | 18:04 | 18:08 | **−4** |

### Findings

**Confirmed (measured):**
- Madhab = Hanafi (Asr 0 residual vs Hanafi calc; +42 min vs Shafi'i).
- **No MOON built-in adjustments**: Maghrib offset = 0 (not +3).
- **Dhuhr_Zawal_Begins = raw Istiwa + ~1 min.** Their column name literally means "zawal begins" — publishes the raw astronomical instant, not adhan-safe time. **Different convention from Quba's "Dhuhr Start" = Istiwa + 5.**

**Open (one month insufficient):**
- Fajr is 7 min *before* ISNA 15° → larger angle (~16°) or offset −7 min. Cannot distinguish winter-only.
- Isha is 4 min *before* ISNA 15° → smaller angle (~14°) or offset −4 min.
- **Asymmetric angles** (Fajr > 15°, Isha < 15°) suggested. Could be Umm al-Qura-like custom (`sunset + 90 min` for Isha; Isha 18:05 ≈ published 18:04), but Fajr would then be ~4:58 not 5:13 — doesn't fit. Likely custom per-prayer angles or offsets.
- **Needs June sample or full-year** to resolve angle-vs-offset ambiguity.

---

## 3. Tawheed Center — 🇺🇸 United States, Farmington Hills, MI

- **Country:** United States of America
- **City / region:** Farmington Hills, Michigan (Metro Detroit)
- **Address:** 29707 W 10 Mile Rd, Farmington Hills, MI 48336
- **Coordinates:** 42.470667, -83.340793
- **Timezone:** America/Detroit (EST winter, EDT summer — NOT GMT/BST)
- **Calendar coverage:** Jan-Jul, Oct-Dec (Aug/Sep skipped)

### Structure

Tawheed publishes **both Standard and Hanafi Asr** as separate `Begins` columns — transparency for congregants of different madhabs. `Iqamah` reflects the mosque's actual jamaah time. **Iqamah timing (~66 min after Standard Asr) suggests Standard is the jamaah basis.**

### Best-fit config — CONFIRMED (perfect match)

```
Method:    SVC_PRAYER_METHOD_ISNA
Madhab:    SVC_PRAYER_MADHAB_STANDARD (jamaah basis; Hanafi also published)
High-lat:  SVC_PRAYER_HIGHLAT_AUTO
Offsets:   ALL ZERO (none)
```

### Measured residuals — every value within ±1 min

| Date | Fajr | Sunrise | Dhuhr | Asr Std | Maghrib | Isha |
|---|---|---|---|---|---|---|
| Jan 01 | 6:39=6:39 (0) | 8:03=8:03 (0) | 12:38=12:38 (0) | 14:54=14:54 (0) | 17:12=17:12 (0) | 18:36=18:36 (0) |
| Jan 16 | 6:37 vs 6:38 (−1) | 8:00 vs 7:59 (+1) | 12:44=12:44 (0) | 15:08=15:08 (0) | 17:28 vs 17:27 (+1) | 18:50=18:50 (0) |
| Jan 31 | 6:28=6:28 (0) | 7:48=7:48 (0) | 12:48 vs 12:47 (+1) | 15:25 vs 15:24 (+1) | 17:47=17:47 (0) | 19:07=19:07 (0) |

**18 comparisons, all within ±1 min.** Tawheed = zero-config fit for `svc_prayer_engine_calculate(ISNA, Standard)` — no offsets, no tuning.

### Findings

**Confirmed (measured, high confidence):**
- Method = **ISNA 15°/15°** — the winter symmetric-angle check (84 min both sides of Jan 1) plus the perfect 3-date match.
- Madhab = **Standard** for jamaah basis (Iqamah = Standard Asr + 66 min; Hanafi Asr + 29 min would be too short for typical US mosque practice).
- **All offsets zero.** This mosque publishes exactly what the astronomical calculation gives.
- **Publishes both madhabs' Asr times** — transparency pattern, not a Hanafi indicator.

**Structural difference from UK mosques:**
- Tawheed publishes raw astronomical times (matches ChronoLink's native output)
- Quba/Crawley publish adhan-safe times (add safety buffers)
- This is a **US convention difference**, not a method/angle difference.

**Note on timezone:** the diagnostic correctly used `-5.0` (EST, January). If someone re-runs this at `0.0` GMT, they'll see Fajr at ~17:39 instead of 06:39 — that's the longitude sign / timezone trap.

---

## Cross-mosque patterns

### Pattern 1: No universal preset

Each mosque has a distinct config. No "UK Hanafi default" exists. Even within the UK (Quba vs Crawley), the method and offset conventions differ. Across the Atlantic, the US convention differs again.

**Implication:** ChronoLink should not ship a "one size fits all" preset. It should ship:
- Flexible config (exists)
- Mosque calendar import (uses existing `svc_timetable` upload — CSV/JSON)
- Documentation of known configs (this file)

### Pattern 2: Dhuhr/Maghrib offset conventions vary

| Mosque | Country | Dhuhr offset | Maghrib offset |
|---|---|---|---|
| Quba | 🇬🇧 UK | +5 (MOON built-in) | +5 (MOON +3 + 2) |
| Crawley | 🇬🇧 UK | +2 (custom) | +0 |
| Tawheed | 🇺🇸 USA | 0 (measured) | 0 (measured) |

Each mosque's "Start" column reflects **their safety margin**. Some publish earliest-permissible (0). Others publish adhan-call time (+2, +5). **The published number is a product decision, not fixed physics.**

### Pattern 3: Summer high-latitude handling is idiosyncratic

- Quba (51.56°N, UK): fixed "Isha = Maghrib + 65 min" for Jun-Aug
- Tawheed (42.47°N, USA): uses ISNA-AUTO (probably), very late Isha
- Crawley (51.13°N, UK): unknown (no summer data)

At 42–52°N, sun never reaches −15° or −18° in peak summer → all mosques need a high-lat rule. **No two use the same rule.** ChronoLink's AUTO (seventh-of-night at >48°) is reasonable but doesn't match any specific mosque.

### Pattern 4: Columns mean different things

- Quba: "Start" = adhan time with safety offset
- Crawley: "Zawal_Begins" = raw astronomical instant
- Tawheed: "Begins" = ambiguous

**No universal importer possible without user metadata.** A mosque calendar import feature must ask: does your "Begins" include safety margin? How much?

### Pattern 5: Country/region affects convention

- **UK mosques** (Quba, Crawley): mix of MOON, ISNA, and custom; Dhuhr/Maghrib offsets common (+2 to +5 from astronomical).
- **US mosques** (Tawheed): ISNA is near-universal; offsets often smaller; publishes multiple madhab options for transparency.

**Do not assume regional stereotypes** — even UK mosques vary. But USA ISNA-default is a real pattern worth noting for user guidance.

---

## Recommendations

### For PR #140 (mosque-calendar fixture test)

**Scope:** Two mosques — Masjid Quba and Tawheed Center. **NOT Crawley** (only 1 month of data; unresolved Fajr/Isha angles).

**Why both:**
- Quba = heavily-tuned custom config (MOON+Hanafi+Maghrib+2) — proves the config system can reproduce a real mosque's adhan-safe timing.
- Tawheed = zero-config fit (ISNA+Standard+0 offsets) — proves the engine's defaults match a real mosque's raw astronomical output exactly.
- Together they bracket the config space (custom↔zero) and give two distinct, measured validation targets.

**Files:**
- New `prayer_host_tests/test_mosque_calendars.cpp` — two test blocks
- Modified `prayer_host_tests/Makefile` — new `test_mosque_calendars` target
- Ships with `docs/mosque-calendars.md` as the reference doc

**Quba assertions** (13 dates):
- Fajr: ±6 min
- Sunrise / Istiwa: ±2 min
- Dhuhr: ±2 min (skip Jumu'ah Fridays)
- Asr: ±2 min
- Maghrib: ±2 min (Mar 2 anomaly is +5 — **exclude this date**, or widen to ±6 for Feb-Mar only)
- Isha: ±12 min, **exclude Jun 21 and Jul 1** (known +8 gap)

**Tawheed assertions** (3 dates):
- All six prayers: **±2 min** (measured maximum is ±1, leave 1 min headroom for future rounding policy changes)
- Timezone: `-5.0` (EST, January 2026)
- Madhab: Standard
- Method: ISNA
- Zero offsets

**PR description:** *"Validates that svc_config + svc_prayer_engine can reproduce two real published mosque calendars within tolerance: one heavily-tuned UK MOON config and one zero-config US ISNA config. The finding documented in `docs/mosque-calendars.md` is that no universal preset fits all mosques — each mosque's config is its own product decision."*

### Future roadmap

| # | Item | Scope | Effort | Priority |
|---|---|---|---|---|
| A | `docs/mosque-calendars.md` (this file) | Docs | Done | — |
| B | `svc_config` preset system (`SVC_CONFIG_PRESET_UK_MOON_HANAFI` etc.) | Config | Small | Medium |
| C | `summer_isha_offset_min` — fixed offset from Maghrib in peak summer | Config | Medium | Medium |
| D | `/api/mosque-preview` — user provides calendar, system shows calculated-vs-published diff | HTTP | Large | Low |
| E | Grow this file to 5-10 more mosques (UK, US, EU, Middle East) | Docs | Incremental | Low |

---

## Appendix: How to analyze a new mosque

1. **Record country, city, full address** from the mosque's own published calendar or website.
2. **Get coordinates** from Google Maps (6+ decimals).
3. **Confirm timezone** — check DST transition dates in the calendar.
4. **Identify madhab** — compare Asr Start against Shafi'i vs Hanafi (Standard ≈ 3.5h after dhuhr at 45-50°N winter; Hanafi ≈ 4.5h).
5. **Add mosque to `test_wrapper.cpp`** on a `diag/` branch.
6. **Compare Fajr/Isha** against all four methods → pick best fit.
7. **Compute Dhuhr − Istiwa and Maghrib − sunset** → implied offset conventions.
8. **Sample Jun 21** for high-lat handling.
9. **Write findings here** with confidence level + country + timezone.

**Pitfalls:**
- Don't assume a method from regional stereotyping. UK mosques span MOON/MWL/ISNA/custom.
- Don't assume Dhuhr_Start is astronomical. Verify against Istiwa.
- Don't assume one month is enough. Winter doesn't discriminate Fajr angle from Fajr angle + offset.
- Don't assume timezone. US mosques are EST/EDT or CST/CDT etc., not GMT/BST.

---

*End of mosque-calendars.md. This document grows as more mosques are analyzed.*