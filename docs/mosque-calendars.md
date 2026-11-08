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
| Tawheed Center | 🇺🇸 United States | Farmington Hills, MI | ISNA 15/15 | Standard (Hanafi alt) | unknown (US convention) | unknown |

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
- Tawheed — **hand-math + measured** (8 months, symmetric-angle check). High for angle, unknown for offsets.

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

### Best-fit config (preliminary)

```
Method:    SVC_PRAYER_METHOD_ISNA
Madhab:    SVC_PRAYER_MADHAB_HANAFI
High-lat:  SVC_PRAYER_HIGHLAT_AUTO
Offsets:   Fajr 0, Dhuhr +2, Asr 0, Maghrib 0, Isha 0
```

### Comparison (Nov 1 example, Δ = calendar − ChronoLink)

| Prayer | Crawley | ISNA/Hanafi | MWL/Hanafi | KHI/Hanafi | MOON/Hanafi | Best |
|---|---|---|---|---|---|---|
| Fajr | 5:13 | 5:20 (+7) | 5:01 (−12) | 5:01 (−12) | 5:17 (+4) | **MOON** |
| Sunrise | 6:52 | 6:53 (+1) | 6:53 (+1) | 6:53 (+1) | 6:53 (+1) | all |
| Dhuhr | 11:45 | 11:44 (−1) | 11:44 (−1) | 11:44 (−1) | 11:49 (+4) | **ISNA/MWL/KHI** |
| Asr | 14:46 | 14:44 (−2) | 14:44 (−2) | 14:44 (−2) | 14:44 (−2) | all (Hanafi) |
| Maghrib | 16:35 | 16:34 (−1) | 16:34 (−1) | 16:34 (−1) | 16:37 (+2) | **ISNA/MWL/KHI** |
| Isha | 18:04 | 18:08 (+4) | 18:21 (+17) | 18:27 (+23) | 18:00 (−4) | **MOON or ISNA** |

### Findings

**Confirmed:**
- Madhab = Hanafi (Asr 14:46 = 42 min after Shafi'i 14:04).
- **No MOON built-in adjustments**: Dhuhr is Istiwa + 2 (not +5); Maghrib is sunset + 0 (not +3).

**Open:**
- Fajr between ISNA and MOON. Could be either + small offset.
- Isha straddles ISNA/MOON.
- **One month cannot distinguish Fajr angle from Fajr angle + offset** — winter times don't spread widely. Needs summer or multi-month data.

**Column naming detail:** Crawley labels the Dhuhr column `Dhuhr_Zawal_Begins` — "the moment zawal begins". Different convention from Quba (where "Dhuhr Start" includes the +5 safety offset). Suggests mosques publish times with different meanings.

---

## 3. Tawheed Center — 🇺🇸 United States, Farmington Hills, MI

- **Country:** United States of America
- **City / region:** Farmington Hills, Michigan (Metro Detroit)
- **Address:** 29707 W 10 Mile Rd, Farmington Hills, MI 48336
- **Coordinates:** 42.470667, -83.340793
- **Timezone:** America/Detroit (EST winter, EDT summer)
- **Calendar coverage:** Jan-Jul, Oct-Dec (Aug/Sep skipped)

### Structure

Tawheed publishes **both Standard and Hanafi Asr** as separate `Begins` columns — transparency for congregants of different madhabs. `Iqamah` reflects the mosque's actual jamaah time.

### Best-fit config (preliminary)

```
Method:    SVC_PRAYER_METHOD_ISNA
Madhab:    SVC_PRAYER_MADHAB_STANDARD (jamaah basis; Hanafi also published)
High-lat:  SVC_PRAYER_HIGHLAT_AUTO
Offsets:   unknown (US convention; likely small or 0)
```

### Winter symmetric-angle check (Jan 1)

| Metric | Value | Implied angle |
|---|---|---|
| Sunrise − Fajr gap | 8:03 − 6:39 = **84 min** | **15.1°** (ISNA 15°) ✓ |
| Isha − Sunset gap | 18:36 − 17:12 = **84 min** | **15.0°** (ISNA 15°) ✓ |

Perfect symmetry. **Hard confirmation of ISNA 15/15.**

### Findings

**Confirmed:**
- Method = ISNA (15°/15°) — symmetric gaps exact.
- **Country = USA, timezone America/Detroit (EST/EDT).** Not UK — no GMT/BST.

**Open:**
- Dhuhr/Maghrib offsets — US convention, likely 0 or small. Not measured.
- Jamaah basis: Iqamah 4:00 PM after Standard Asr 2:54 PM = 66 min gap (typical US). After Hanafi Asr 3:31 PM = 29 min gap (short). **Inferred: Standard jamaah basis** despite publishing Hanafi.
- Summer Isha: up to 11:11 PM EDT in June. Very late but plausible for 15° at 42.47°N summer with high-lat rule.

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
| Tawheed | 🇺🇸 USA | unknown | unknown |

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

### For PR #140 (Quba fixture test)

**Scope:** Fixture test only for Masjid Quba. Do NOT try to fit Crawley or Tawheed in this PR.

- New `prayer_host_tests/test_masjid_quba.cpp`
- 13-date fixture at Quba's coordinates
- Config: MOON + Hanafi + Maghrib +2
- Assertions: Fajr ±6, Sunrise ±2, Istiwa ±2, Dhuhr ±2, Asr ±2, Maghrib ±2, Isha ±12 (or skip summer per known gap)
- PR description explicitly states: *"This validates that svc_config + svc_prayer_engine can reproduce one specific UK mosque's published calendar. It does NOT generalize — other UK mosques use different configs (see docs/mosque-calendars.md)."*

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