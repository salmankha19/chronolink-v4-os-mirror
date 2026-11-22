# Project Status

Snapshot reviewed 2026-11-15 at commit `HEAD` (PRs: `hijri_offset_days` algorithmic Kuwaiti-tabular offset #146; `hijri_calc_method` Umm al-Qura KSA bitmask with confirmed 1447/1448 AH windows #147). Update this file in the same change that alters any item below.

**This is the authoritative list of what is implemented vs. what is
not.** Where another doc mentions a gap, this file has the detail.

---

## Design intent

- No user-facing buttons. BOOT/EN on the PCB are development aids.
- All user configuration — WiFi, time, prayer settings, display,
  firmware updates, factory reset — goes through a web UI.
  The web UI is **in progress**; see "Web / config work (in progress)".
- Prayer times, adhan, and jamat follow the three-tier model in
  [prayer-times-model.md](prayer-times-model.md).

---

## Implemented (working code)

- **Boot:** logging, factory reset (parameter flag or
  `POST /api/factory-reset` persistent request), NVS, FATFS `/storage`,
  OTA partition logging, hand-off to kernel.
- **App rollback (PR 4.6, extended 2026-11-15):** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`;
  `app_main()` calls `esp_ota_mark_app_valid_cancel_rollback()` at the
  end, after every service (`svc_config`, `svc_time`, `svc_timetable`,
  `svc_prayer_engine`, `svc_network`, `svc_http`, `svc_scheduler`) has
  started. (#129 R2 moved this call from `boot_manager_init()`, where it
  ran before any service was up.) An image that crashes or resets before
  that point is reverted to the previous slot on the next reset.
  **(CR #3, 2026-11-15)** Further gate: if `svc_http_start()` fails,
  `app_main()` calls `esp_ota_mark_app_invalid_rollback_and_reboot()`
  instead of mark-valid, because a firmware with no HTTP server has no
  recovery path (no config UI, no OTA endpoint, no reboot endpoint). The
  other services stay non-fatal: svc_time/svc_network/svc_scheduler
  failures leave the clock running and the device recoverable on the
  next flash. A "stability delay" (mark valid after N seconds of clean
  run) is deliberately not added — it would delay every healthy boot
  for no failure mode it would actually catch.
- **OTA over HTTP (PR 4.7):** `POST /api/ota` in `svc_http` streams a
  raw firmware image (`application/octet-stream`) to the inactive OTA
  slot via `esp_ota_begin` / `esp_ota_write` / `esp_ota_end` /
  `esp_ota_set_boot_partition`, then reboots. Guarded by a compile-time
  `X-OTA-Token` shared secret (`OTA_SHARED_TOKEN`) and a build-time
  `#error` if `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is not set. On any
  failure calls `esp_ota_abort()` and leaves the running image
  untouched.
- **svc_http security batch (PR 3):** every numeric config field is
  validated (integral + range) before any int cast, closing the
  `{"high_lat_rule": 257}` → 1 truncation bypass on `uint8_t` and
  `uint16_t` fields. All state-changing endpoints (`POST /api/config`,
  `/api/config/preset`, `/api/wifi`, `/api/reboot`, `/api/factory-reset`,
  `/api/timetable`, `/api/timetable/regenerate`, `DELETE /api/timetable`,
  and `POST /api/ota`) require `X-Chronolink-Auth: Bearer <16-hex>` where
  the token is generated on first boot, stored in NVS namespace
  `svc_http` key `api_token`, and printed once to serial. OTA refuses to
  run while `OTA_SHARED_TOKEN` is still the shipped default.
- **API token length: 16 hex chars (64-bit), reduced from 64 (256-bit).**
  Short enough to type on a phone, which is what an on-site debug session
  actually needs. 64-bit security is adequate for local network admin
  operations on a mosque prayer clock: the main threat is someone already
  on the mosque WiFi, and that is mitigated by the WiFi password; the
  token's job is to block CSRF and stray writes, not to resist an attacker
  who holds that password or physical access. The change is self-healing on
  upgrade -- a stored token whose length no longer matches is discarded and
  a fresh one is generated and persisted at boot -- but the installer must
  re-paste the new value once. `www/index.html` validates 16 lowercase hex
  chars and drops a stale token from `localStorage`; `src/index_html.c` is
  regenerated with `scripts/embed_html.py`.
  Upload/OTA streaming now fails after a bounded number of consecutive
  socket timeouts (upload ~10 s, OTA ~30 s) instead of looping forever;
  upload also checks `fwrite` and rejects short writes.
- **PDL:** pin map, capability flags, pin validation and
  pre-configuration.
- **HAL:** GPIO, I2C (mutex-guarded), shared SPI (mutex-guarded),
  DS3231M RTC with oscillator-stop detection and ISO-8601 weekday
  computation on write, RTC-absent short-circuit, and hour/date/year
  validation on set, display router, ST7796S
  backend, `hal_gfx`, sensor registry with SHT4x/BME280/VEML7700
  drivers, MAX98357A audio, driver SDK for one custom display driver.
- **HIL adapters** for display, RTC, sensors, audio, GPIO, I2C, SPI.
- **Kernel:** event bus, tasks, safe-mode skip, and a kernel-owned
  `event_dispatch_task` created first in `core_os_start()`.
- **Event bus consumer:** `event_dispatch_task` (`core_os`) drains the
  kernel event queue, so publishes no longer fill the queue and evict
  older entries. It logs each event at DEBUG and then calls
  `event_bus_dispatch()`. `event_bus_subscribe(q, type)` registers a
  queue to receive copies of selected events; `event_bus_dispatch()`
  forwards matching events non-blocking, up to
  `EVENT_BUS_MAX_SUBSCRIBERS` (8) entries. `svc_time` is the first
  subscriber (PR 5b-2): it registers for `EVENT_NETWORK_CONNECTED`.
- **Services:** `svc_network` (WiFi station), `state_manager`, `svc_config` (NVS-backed installation config: location, method, madhab, iqamah offsets, adhan mode, display preferences, high-latitude rule, adhan offsets, zawal buffer — storage + accessors only, no calculation or hardware). **PR 5.1a additions:** seasonal jamat config (`jamat[5]` with per-prayer winter/summer anchor+offset and shared rounding, plus a `season` struct with fixed summer/winter transition dates) and named presets (`SVC_CONFIG_PRESET_UK` default, `SVC_CONFIG_PRESET_PAKISTAN`). Legacy `iqamah_offset_min[]` retained as deprecated mirror of `jamat[i].winter.offset_min` for backward compatibility (removal in 5.1c; `refactor(config): drop iqamah mirror`). **PR #146/#147 additions:** `hijri_offset_days` (`int8_t`, -3 to +3) and `hijri_calc_method` (`uint8_t`, 0=Kuwaiti, 1=Umm al-Qura) replace the old `ramadan_windows[]` table. Uses an algorithmic Gregorian-to-Hijri conversion (with a 158-year KSA bitmask) to compute `is_ramadan`.
- **Hijri date display (2026-11-15, numeric format):** `svc_config_get_hijri_date()`
  exposes the Islamic calendar to other layers; `svc_prayer_engine_service`
  populates `svc_prayer_engine_times_t.hijri_{year,month,day}` plus full
  and short month-name strings; `GET /api/prayers` gains a `hijri` object;
  the web UI shows `18 Jumada al-Ula 1447 AH`; the clock face draws
  `Sun 15 Nov 2026` and `18/05/1447` below `H:MM AM/PM` (numeric form —
  uniform width, language-agnostic, Latin digits; no non-English month
  bitmaps needed). Both date
  lines are hidden when the RTC is untrusted. During Ramadan, the Hijri
  line naturally reads `7 Ramadan 1447` — no separate day counter. Fixed
  `jdn_to_hijri_uaq()` pre-1924 fallback (was `jd-1`, now `jd`) and
  post-table fallback (was `jd-1`, now `jd+1`); both latent because the
  firmware guards year `[2020, 2100]`, reachable only via the
  host-compilable `hijri_calendar.c`. New tests in
  `prayer_host_tests/test_hijri_calendar.cpp`: UAQ vs tabular parity at
  1447 AH, pre-1924 and post-2076 fallback regressions, offset interaction
  with the UAQ path, month-name bounds.

- **`svc_prayer_engine` (PR 13):** pure C++ prayer time calculation. Standalone library in `components/svc_prayer_engine/include/prayertimes/` (no ESP-IDF dependency; usable outside this project). Computes Fajr, Sunrise, Dhuhr, Zawal Start, Asr, Asr Yellowing (preferred end), Maghrib, Sunset (true astronomical), Isha, Islamic Midnight for a given date, coordinates, timezone offset, and method (MWL, ISNA, Egyptian, Karachi, Umm al-Qura, Jafaria, Moonsighting Committee, Custom Angles). High-latitude fallback supports AUTO (seventh-of-night above 48° latitude, middle-of-night at or below 48°), plus explicit NONE, MIDDLE_NIGHT, SEVENTH_OF_THE_NIGHT, and ANGLE_BASED rules selectable via the wrapper. Default is AUTO. **Verified against AlAdhan API** across four global cities (Horsham, Karachi, Makkah, New York) — Fajr/Sunrise/Dhuhr/Maghrib/Isha within ±30 s after PR 19 (sub-minute precision). As of PR #133 the engine rounds start-of-window fields **up** and end-of-window fields **down** to the whole minute before publication (see [prayer-times-model.md](prayer-times-model.md) §2.1); every published field remains minute-aligned, and displayed times may differ from the sub-minute AlAdhan comparison by up to one minute in the safe direction (never earlier than the astronomical instant for start-of-window; never later for end-of-window). The underlying astronomical calculation is unchanged. Asr is intentionally *not* compared to AlAdhan: AlAdhan samples solar declination at noon, while this engine samples at the Asr instant, so the post-PR-19 Asr differs from AlAdhan by up to ~100 s in favour of the more accurate iterated declination. Wrapper API: `svc_prayer_engine_calculate()`. The high-latitude fallback uses clamping (PrayTimes.org style) rather than NaN-only. AUTO selects one-seventh-of-night whenever |lat| > 48° or any twilight angle the method uses is unreachable at the summer solstice (|lat| > 66.5 − angle). The unreachable-angle branch covers Egyptian from ~47.0°N — the exact-48° boundary is no longer special-cased. Middle-of-night is used otherwise (Middle East, South Asia). Both branches depend only on latitude + method, so the rule cannot flip mid-year.

  Islamic midnight is max(sunset→clamped-Fajr midpoint, Isha) — never earlier than Isha, and continuous day-to-day (the old "fall back to a sunrise-based definition when midnight < isha" jumped by up to 1.4 h near the clamp boundary). Isha and midnight are computed as offsets from the wrapped maghrib/sunset values, so the maghrib→isha and sunset→midnight intervals stay correct even when civil time is far from solar time (western Alaska, Jafaria at ~60°N). (PR 18) Jafaria method reachable: `svc_config_method_t` extended to `JAFARIA = 5, COUNT = 6`, matching `svc_prayer_method_t` 1:1. Date validation rejects non-existent days (e.g. 31 Feb, 29 Feb in non-leap years). `asr_yellow_sec == UINT32_MAX` when the sun never reaches 5° above the horizon (high-latitude winter); distinct from `valid == false`. On error, the API returns without writing `*out` — callers must check the return code before reading fields. (PR 19) Sun-position sampling is now iterated per event rather than fixed at 0h UT: `mid` is converged once, then each angle event and Asr is iterated 2-3× at `jd − lon/360 + t/24` until the declination sample stabilises (<1 s residual). This closes the equinox accuracy gap Claude's PyEphem review measured at ±125 s, verified on hardware 2026-10-17: Fajr/Sunrise/Dhuhr/Asr/Maghrib/Isha deltas vs the 0h-UT baseline exactly match the Python `mode2` prediction (+15 / +25 / −6 / −59 / −88 / −92 s).
- **`svc_timetable` (PR 5.5):** year-calculated prayer timetable at
  `/storage/timetable.json`. Firmware regenerates on boot / on any
  prayer config change; user can upload a mosque's own timetable. Both
  file types store final local-time seconds. Per-field fallback on
  **read** (never on upload): missing fields are computed from config
  each recalculate. Tripwire compares file values against a fresh
  engine call (>60 s mismatch serves fresh) — **firmware files only**
  (PR #125); user uploads are served verbatim per
  [prayer-times-model.md](prayer-times-model.md) §5.5. Year guard
  `[2020, 2100]`. Jamat bracket validation rejects jamat < adhan, warns
  if jamat ≥
  next prayer. Atomic write via `.tmp` + rename. Config-change warning:
  server returns 409 for partial user file unless client confirms.

- **`svc_timetable` schema v3 → v5 (feat/hijri-offset-days →
  feat/hijri-umalqura-option):**
  `SVC_TIMETABLE_SCHEMA_VER` bumped from 3 to 4, then from 4 to 5. A v3 file
  carries `"rw":[...]` (the removed `ramadan_windows[]` overrides) in the
  `calculation` block; v4 carries `"hijri_offset":N`; v5 carries
  `"hijri_offset":N` plus `"hijri_method":N` (the selected Hijri calendar, so
  a method change now also trips regen). Firmware files regenerate on the
  `cJSON_Compare` mismatch; a v<5 user upload loads silently but emits a
  one-shot `ESP_LOGW` so the operator can re-export.
  User files are still **never** auto-regenerated (mosque authority).
  Version history is documented in `prayer-times-model.md` §5.5.

- **`svc_timetable` hardening (PR 4):** internal state mutex (all public
  accessors task-safe), writer mutex shared by upload / regenerate / background
  regen, split temp files (`timetable_upload.tmp` / `timetable_regen.tmp`),
  and upload validation via `svc_timetable_validate()` before the atomic
  rename. Rejected uploads leave the live timetable untouched and return 400;
  a busy writer returns 503.
- **`svc_prayer_engine_service` (PR 15, extended PR 5.1a):** runtime service that reads `svc_config` (location, method, madhab, high-latitude rule, adhan offsets, zawal buffer) and `svc_time` (local date), calls `svc_prayer_engine_calculate()`, applies per-prayer adhan offsets (`adhan_offset_min[]`), computes zawal start (`dhuhr - zawal_buffer_min`), and publishes `EVENT_PRAYER_TIMES_UPDATED`. **PR 5.1a additions:** publishes a three-tier snapshot — `astronomical_*` (astronomical calculation + timezone, no user offsets), `*_sec` (adhan = astronomical + `adhan_offset_min[]`), `jamat_sec[]` (seasonal jamat, with per-prayer rounding) — plus auxiliary times (`sunrise_sec`, `sunset_sec`, `midnight_sec`, `zawal_start_sec`, `asr_yellow_sec`), Ramadan endpoints (`sehri_end_sec`, `iftar_sec`), and `jamat_tomorrow_sec[]` for the "tomorrow changes" notification. Consumers read finished values; no offset math in the UI. Public API: `svc_prayer_engine_service_start()`, `_recalculate()`, `_get_times()`. The event is a signal only (payload truncated to 32 bytes); consumers must call `_get_times()` for the full snapshot. Recalculation is triggered by `EVENT_TIME_UPDATE` (successful NTP sync) and by `EVENT_TIME_DATE_CHANGED` (local date or UTC offset change). (PR 17) Rolls over at midnight and at DST transitions: `svc_time` publishes `EVENT_TIME_DATE_CHANGED` when the local calendar date or the effective UTC offset changes; the service subscribes, recalculates, and self-heals within 60 s if the event is dropped. Timezone offset is taken at **noon of the target date** (not at call time), so a DST-transition day's prayer times all use the same offset. **(PR 5.8)** `recalculate()` split into three files with no behavior change: `src/adhan_calculator.{c,h}` (engine call + `adhan_offset_min[]`; returns pre-merge `raw` plus offset-applied `adhan_sec[5]`) and `src/jamat_calculator.{c,h}` (pure seasonal jamat arithmetic — season, anchor, offset, rounding). `merge_adhan()`, `merge_jamat()` and `TT_TRIPWIRE` stay in the service because they consume parsed timetable input and log. Jamat anchors are fed the **effective** (post-merge, post-tripwire) adhan tier via `adhan_result_t`; passing raw engine output would have changed Fajr/Isha jamat for uploaded mosque timetables that override sunrise/midnight. Verified byte-identical on hardware (`j_fajr=21900 j_dhuhr=46800 j_asr=51900 j_maghrib=60240 j_isha=66000`, `jamat change tomorrow` lines unchanged) and by a 305,489-case host differential test against the pre-split inline algorithm. Commits `e0d0571` (adhan) and `98c5fc0` (jamat). Design intent for the whole prayer-times model is in [prayer-times-model.md](prayer-times-model.md) §8.1.

- **`svc_scheduler` (PR 5.6):** prayer time scheduler. A 1 Hz task on core 0
  (`scheduler`, `tskIDLE_PRIORITY + 2`, 4 KB stack) reads
  `svc_time_get_epoch()` + `svc_prayer_engine_service_get_times()` and
  publishes `EVENT_PRAYER_TIME_REACHED` (id 97; payload `prayer_time_event_t`
  in `components/event_bus/include/time_events.h`) exactly once per prayer
  per day, adhan tier only (`PRAYER_KIND_JAMAT` reserved). Subscribes to
  `EVENT_TIME_DATE_CHANGED` (6) and `EVENT_PRAYER_TIMES_UPDATED` (96); both
  re-fetch the times, and -- only if the epoch read and `_get_times()` both
  succeed -- clear `last_fired_epoch[]` and pre-mark the prayers that already
  passed without publishing. A failed fetch/epoch read leaves
  `last_fired_epoch[]` and `pending_isha_epoch` intact (as of #136), so one
  NTP-step hiccup cannot make today's already-fired prayers fire twice.
  `svc_scheduler_init()` does the
  same pre-marking at boot, so a 13:00 boot does not replay the 10:00 and
  12:00 adhans. **Event subscriber state (as of #136):** `EVENT_PRAYER_TIME_REACHED`
  has zero subscribers -- the scheduler publishes correctly, but the intended
  consumers (`svc_audio` at Phase 3, `ui_producer` for display state) are not
  wired yet. Wiring deferred until Phase 3 audio lands, at which point the
  payload already carries everything a consumer needs (`prayer_id`, `kind`,
  `scheduled_epoch`). Documented here as intentional, not a defect.
  Decision logic is pure C (`scheduler_logic.c`: no ESP-IDF, no
  FreeRTOS, no heap) and host-tested in
  `prayer_host_tests/test_scheduler.cpp` -- part of `make test`: happy path,
  DST fall-back / repeated local hour, mid-day startup, config reset,
  premark grace, lateness cap, the exact-midnight prayer (`sec[p] == 0`), and
  the `UINT32_MAX` / `valid == false` sentinels. **Invariant: epoch comparison
  only -- no `(hour, minute)` equality anywhere**, so a repeated local hour or
  an NTP step can neither double-fire nor skip a prayer. **Lateness cap:**
  a prayer more than `SCHEDULER_MAX_LATENESS_S` (120 s) in the past is
  treated as missed -- marked fired without being published -- so a stalled
  or stopped task cannot produce a burst of five catch-up adhans. Within
  the cap the old catch-up semantics are unchanged. Out of scope in v1:
  jamat firing, DS3231 INT wake (ROADMAP Phase 6), audio (Phase 3).
  **Known limitation:** `tz_offset_minutes` is the offset at noon of the
  target date, so on a spring-forward / fall-back day a prayer before the
  transition can fire up to 1 h early / late (ROADMAP Phase 7). Fixing it
  needs a per-prayer offset in `svc_prayer_engine_times_t`; it is not a
  `svc_scheduler` bug and must not be worked around there.
  **Isha past local midnight (PR #124):** preserved across the day
  rollover via `scheduler_state_t.pending_isha_epoch`; fires once at its
  real epoch (or is marked missed if stale by >120 s). Covered by
  `test_scheduler::test_isha_rollover`. No change to
  `svc_prayer_engine_times_t` — the adapter recovers the deferred epoch
  from the previous `scheduler_day_t`.
  **Publish reliability (CR #1, 2026-11-17):** `scheduler_tick()` publishes
  via `event_bus_publish_checked()`. It snapshots `pending_isha_epoch` and
  `last_fired_epoch[]` before `scheduler_evaluate()` (which commits both
  before returning). On a failed publish -- bus full after the evict-oldest
  retry, or no queue -- it rolls the affected `last_fired_epoch[id]` back to
  its pre-evaluate value so the next 1 Hz tick re-fires, bounded by
  `SCHEDULER_MAX_LATENESS_S` (120 s; beyond that the prayer is missed, same
  as any other stall). For a failed pending Isha (id 4) it also restores
  `pending_isha_epoch`: resetting `last_fired_epoch[4]` alone would not
  retry, because `scheduler_evaluate()` had already consumed the pending
  epoch and today's Isha may still be in the future. Every other
  `event_bus_publish()` caller keeps the void wrapper (boot, network, time,
  prayer-engine snapshot) -- those signals self-heal on the next cycle.
  `scheduler_logic.c` and its host tests are unchanged. The retry path fires
  only on a full bus, so it is CI build-verified only, not host-tested.
- **Application (layer 6):** `display_manager`, `ui_producer`,
  `font_engine`, `font` -- UI and display logic, not services
  (Constitution section 11).
- **`svc_time`:** RTC + NTP + POSIX TZ; the first event-bus subscriber
  (`EVENT_NETWORK_CONNECTED`); publishes `EVENT_TIME_UPDATE`; ticks the
  snapshot once a second (PR 5c). Details in the `svc_time` section below.
- **Phase 3 time & prayer state machine (`feat/time-state-machine`):**
  hourly RTC correction (system clock snaps to RTC once per clock hour
  if drift > 1 s), 23:00 daily NTP sync (fresh RTC time once per local
  date; skipped if offline; no retry on failure), and
  `svc_time_get_last_sync_time()` telemetry (UTC epoch of last successful
  sync, persisted to NVS `time/last_sync_utc`). `svc_prayer_engine_service`
  delays a pure date rollover recalculation until 00:01 (DST still
  recalculates immediately). Details in the `svc_time` section below.
- **Live clock face (PR 6):** `ui_producer` polls `svc_time` at 250 ms and
  submits a frame only when the formatted local string changes. (Superseded
  by PR 8's scaled RGB565 text and PR 5.5b's single-string + optional
  banner architecture; this bullet records the historical PR 6 design.)
  The old placeholder (which only called `display_manager_init()`) is gone.
- **Scalable time face (PR 8):** `ui_producer` renders `H:MM AM/PM`
  via `font_engine`'s new `FONT_FORMAT_RGB565` path. `HAL_GFX_BlitScaled`
  is a new HAL primitive; `FONT_UI_SANS` (id 1) is a placeholder RGB565
  font pending a project TTF (Inter SemiBold, SIL OFL). Layout is
  panel-derived (fits width and height). The mono `FONT_FORMAT_MONO_1BPP`
  path is unchanged and remains the correct representation for 1-bit
  panels (MAX7219, dot-matrix).
  A generator script `scripts/generate_font_ui_sans.py` supports both
  TTF and built-in placeholder modes; the placeholder can be
  regenerated with `--builtin`.

### Test Harness (v1)

`test_harness/` provides time-injection testing for the prayer pipeline.
See `test_harness/README.md` for usage. Catalog of scenarios in
`test_harness/TEST_CATALOG.md` (live document, enforced by
`run_all.py --check-catalog`).

Pre-push check: `.\test_harness\pre_push_check.ps1` (~3 min, no hardware wait).

- **Mock NTP server (`mock_ntp_server.py`):** serves a fake clock over UDP:123.
  The fake clock is anchored at the device's **first request**, so the board
  always boots at the scenario's `start_utc` regardless of boot-time jitter
  (at 600x a 2.5 s boot spread moved the landing point by 26 fake minutes).
  Accelerating the mock does **not** speed the device up -- the device's own
  clock runs at 1x -- so prayer-fire scenarios run at `speed = 1.0` and their
  deadlines are real seconds.
- **Scenario runner (`scenario_runner.py`):** boot banner + board IP from the
  serial log, then two boots: the first gives DHCP and the `/debug/ntp` write
  of slot 0, the second is the one that counts -- `svc_time` refuses a manual
  sync within 60 s of the previous one (`sync skipped (manual)`), so a device
  whose slot 0 still points at `time.google.com` (clean flash, factory reset)
  would keep *real* time and never reach the scenario's start. Slot 0 is
  persisted, so the second boot's automatic sync lands on the mock and the
  device genuinely boots at `start_utc`. Boot 1's sync line is discarded; boot
  2's is fed to the assertion engine (late, so it cannot satisfy a `sync`
  expectation early), and the assertion clock starts from the epoch the mock
  actually served, not from `start_utc`. Every exit path that got as far as
  the `/debug/ntp` write then restores slot 0 to `time.google.com` via
  `restore_ntp_host()` (single shot, 12 s timeout), so a test session no
  longer leaves the board syncing from the PC. The same request also puts the
  board's own latitude/longitude/method back: before seeding, the runner
  snapshots them through the unauthenticated `GET /api/config`
  (`DeviceController.get_config()`) and passes the values to
  `restore_ntp_host(config=...)`, so the seed no longer permanently overwrites
  a real mosque configuration. A board whose snapshot reports `location_valid`
  false is skipped with a logged notice instead of being handed a `0,0`
  location -- the seeded values stay in place, which is the smaller change.
- **Reference scenario (`scenarios/01_single_prayer_fire.py`):** boots at
  11:49:30, asserts exactly one `Dhuhr adhan due` at 11:50:00 and zero Fajr
  fires. The `sync` assertion is anchored to the mock's known UTC
  (`NTP sync ok (network): UTC 2026-10-31 11:49:\d{2}`), not the bare
  `NTP sync ok` prefix, so a device still running on real time cannot satisfy
  it. Verified on hardware 2026-10-31 (three consecutive PASSes, identical
  landing point and fire epoch, ~70 s wall clock).

---

## `svc_time` (implemented in PR 5b-2)

Code: `components/svc_time` (public header `svc_time.h`, source
`svc_time.c`); event payload `time_event_payload_t` in
`components/event_bus/include/time_events.h`.

Shape of the implementation: one worker task (`svc_time`, 4 KiB stack,
priority `tskIDLE_PRIORITY + 3`, core 0) drains a 4-deep mailbox that
`event_bus_subscribe()` bound to `EVENT_NETWORK_CONNECTED` and wakes once a
second otherwise; an `esp_timer` drives the NTP backoff retry; readers use
`svc_time_get_*()` under a mutex. A temporary 5 s serial status print stays
until the panel is verified on real hardware; PR 6 added the display-side
frame log alongside it rather than removing it.

The design decisions below are the contract:

- **RTC stores UTC.** The DS3231M register is written with UTC and
  read as UTC. It is never written with a DST-adjusted or local-time
  value -- the register would then be meaningless without out-of-band
  metadata, and a device that missed a DST transition while offline
  (brownout, no network, powered off across the change) would have no
  way to recover. Linux (`timedatectl set-local-rtc 0`), Android
  (system clock is UTC; zone applied at display) and ESP-IDF's own
  SNTP example all use this model. See `hal_rtc.h` for the same note
  at the driver level.

- **DST applied at read time, in `svc_time`, via the POSIX TZ mechanism.**
  On zone change or boot, `svc_time` calls
  `setenv("TZ", "<posix_tz>", 1)` followed by `tzset()`, then uses
  `localtime_r()` on the UTC time for display. The C library's (picolibc
  on this target) TZ handling applies the DST rule automatically; no
  "midnight transition handler" code is required. A timer at the next
  transition instant may be registered purely to trigger a UI redraw --
  it does not perform any correction itself.

- **Zones come from a small POSIX TZ string table**, not a bundled IANA
  tzdata. The dropdown covers ~5-6 zones with their TZ strings hardcoded
  in `svc_time`. For zones outside the dropdown, the web-config side
  resolves a pasted Olson ID (`Europe/London`) to a POSIX TZ string and
  pushes that; the device never parses IANA tzdata.

- **NTP is the only sync source.** It provides UTC; `svc_time` writes
  UTC to the RTC and re-derives local on the display path.

#### Phase 3 Additions (Implemented — branch `feat/time-state-machine`)

The following state machine logic (documented in `Architecture.md` §4.1) is now implemented. Each item is described below as it behaves.

*Implementation Note: The logic below is subject to continuous improvement. If a better sequence is found, update `Architecture.md` first. Per `.clinerules`, this work must be done on a dedicated branch (e.g., `feat/time-state-machine`), not directly on `main`.*

1. **Hourly ESP32 Clock Correction (From RTC, no Wi-Fi):** `svc_time_worker` runs an RTC-vs-system drift check once per clock hour, guarded by `s_last_hourly_correction_key = yyyymmddhh`. If the drift exceeds 1 second, `settimeofday()` snaps the system clock to the RTC. The trigger is "first tick of each clock hour", not an exact-`XX:00:00` match, so a delayed tick still fires exactly once. If the RTC read fails or returns an invalid timestamp, the check is skipped silently (logged at DEBUG).
2. **23:00 One-Shot NTP Sync (corrects RTC, requires Wi-Fi):** `svc_time_worker` fires one NTP sync when the local clock first reaches 23:00, guarded by `s_last_daily_sync_date = yyyymmdd`. The sync runs in a dedicated `daily_sync_task` so the 1 Hz worker tick continues during the up-to-10 s NTP block. If Wi-Fi is offline, the sync is skipped (no retry). If the sync fails, no backoff is scheduled — the next attempt is tomorrow at 23:00. Wi-Fi state comes from `EVENT_NETWORK_CONNECTED` / `EVENT_NETWORK_DISCONNECTED`, so `svc_time` does not call `svc_network` directly (Constitution §10).
3. **00:01 Prayer Delay:** `svc_prayer_engine_service` sets `s_date_changed_pending` on the first tick of 00:00 and recalculates on the first tick where the local clock is past 00:00. This gives the system one minute of settling time after the date rollover. A DST / UTC-offset change (also published by `svc_time` as `EVENT_TIME_DATE_CHANGED`) is distinguished from a date change by comparing the cache's `tz_offset_minutes` field — a pure offset change recalculates immediately, no delay. `EVENT_TIME_UPDATE` (successful NTP sync) also recalculates immediately and clears any pending deferral.
4. **Sync Telemetry & NVS Storage:** every successful NTP sync persists its UTC epoch to NVS namespace `time`, key `last_sync_utc`. On boot, the value is read back. `svc_time_get_last_sync_time(time_t *out)` returns it (or `ESP_ERR_NOT_FOUND` if never synced). A future `svc_http` can use this to warn the user if the clock has not synced in a long time.
5. **RTC Trust Policy (PR 5.5b):** `svc_time` reads the RTC even if the oscillator-stop flag (OSF) is set. `svc_time_snapshot_t.rtc_untrusted` is true if OSF is set OR the RTC year is strictly less than the firmware build year (parsed from the `__DATE__` compile-time macro). The UI displays `--:--` and a warning banner after 60 seconds if untrusted. Prayer times are not published while untrusted.

#### NTP servers, retry and TZ storage (design agreed 2026-10-14)

Three NTP slots live in NVS namespace `time`:

| Slot | NVS key | Built-in default |
|---|---|---|
| 0 | `ntp_server_0` | `time.google.com` |
| 1 | `ntp_server_1` | `time.cloudflare.com` |
| 2 | `ntp_server_2` | `0.uk.pool.ntp.org` |

- **Per-slot semantics:** an absent key falls back to the built-in
  default for that slot; a present-but-empty value skips the slot; any
  other value is used as the user's hostname. To run a single custom
  server, set slot 0 and leave slots 1 and 2 empty.
- **Requires `CONFIG_LWIP_SNTP_MAX_SERVERS=3`.** Set in
  `sdkconfig.defaults` by PR 5b-2 (it was LwIP's default, `1`).
  `svc_time` clamps to whatever the build has and logs a warning, so a
  stale `sdkconfig.single_mcu` costs slots rather than a failed sync.
  Changing `sdkconfig.defaults` does not take effect until
  `sdkconfig.single_mcu` (and ideally `.pio/build/single_mcu`) is
  deleted before the rebuild.
- **Retry on NTP failure:** backoff 1 min -> 5 min -> 15 min -> 1 h cap,
  driven by the service's only `esp_timer`. A new
  `EVENT_NETWORK_CONNECTED` cancels the pending backoff and resyncs
  immediately; a successful sync earlier than 60 s ago is skipped, so a
  reconnect storm does not re-run NTP. Periodic resync is left to IDF's
  built-in 1 h cycle (`CONFIG_LWIP_SNTP_UPDATE_DELAY`); no second timer
  is added for it.
- **Timezone storage:** NVS key `tz` in namespace `time`, holding a POSIX
  TZ string; default `GMT0BST,M3.5.0/1,M10.5.0` (UK, DST-aware) with a
  warning log when the key is absent.
  POSIX TZ strings only -- the web UI translates an IANA zone to POSIX
  and may store an optional `ntp_region` key for display; that UI arrives
  with `svc_http`.
- **1 Hz tick (PR 5c):** the worker wakes every second and refreshes the
  snapshot -- from the RTC when one answers, otherwise from the libc clock
  once NTP has stepped it -- so `svc_time_get_*()` advances instead of
  returning the value captured at sync time. The default TZ is the UK POSIX
  string `GMT0BST,M3.5.0/1,M10.5.0` (GMT/BST with automatic DST): a
  dev/QA target until `svc_http` can store the user's zone.
- **RTC Trust & Invalid UX (PR 5.5b):** The snapshot gains `rtc_untrusted`.
  The UI shows a "Starting..." splash on boot, then `--:--` if the RTC is
  untrusted (OSF set or year < build year). A two-line warning banner
  appears after 60 seconds. Prayer times are blocked from publishing while
  untrusted. `DM_MAX_TEXT_LEN` bumped from 16 to 32 in display_manager.h
  to accommodate the banner text.
- **Known IDF limitation (IDFGH-18398):** on a timeout SNTP may retry the
  same server instead of advancing to the next index, so the three-slot
  fallback is **best-effort, not a guarantee**. Live in the code: the sync
  path hands the servers over in slot order and does not work around it.

The `hal_rtc.h` timezone contract comment records the same decision at
the HAL boundary, so a future contributor editing the driver cannot
add local-time or DST logic there without contradicting a written
contract.

---

## `svc_network`

Code: `components/svc_network` (public header `svc_network.h`, source
`svc_network.c`). Station-first, and it never gives up: reads `ssid` / `pass`
from the NVS namespace `wifi`, connects, and retries with a 1 s -> 30 s
exponential backoff (#131 R1 -- a disconnect never drops straight to AP mode).
Since PR #157 the retry is bounded for a *configured* device: 24 consecutive
failures of any reason raise the provisioning AP instead of retrying forever. A
missing or empty SSID takes the same provisioning AP from boot. Publishes `EVENT_NETWORK_STARTING`,
`EVENT_NETWORK_CONNECTED` (on `IP_EVENT_STA_GOT_IP`, not on
`WIFI_EVENT_STA_CONNECTED`), `EVENT_NETWORK_DISCONNECTED` and
`EVENT_NETWORK_ERROR` with an `SVC_NETWORK_STAGE_*` stage.

Public API: `svc_network_start()`, `svc_network_stop()`,
`svc_network_is_connected()`, `svc_network_is_ap_mode()`,
`svc_network_get_ip_str()`, `svc_network_set_credentials()`, plus the unified
provisioning-AP accessors listed below (`svc_network_get_ap_ssid()`,
`svc_network_get_ap_passphrase()`, `svc_network_touch_recovery_idle_timeout()`).

### WiFi provisioning mode (CR #2 recovery path + CR #4 unified AP; hardware verification pending)

Full design: `docs/sessions/cr2-wifi-lockout-design.md`. Architecture summary:
`docs/Architecture.md` section 4.3. Boot-time behaviour and the power-cycle
escape hatch: `docs/boot.md`.

| Constant | Value | Meaning |
|---|---|---|
| `SVC_NETWORK_AUTH_FAIL_THRESHOLD` | 5 | consecutive auth-class disconnects (202/15/204/2/205) that raise the recovery AP |
| `SVC_NETWORK_UNREACHABLE_FAIL_THRESHOLD` | 24 | consecutive disconnects of **any** reason that raise the recovery AP (slow path, PR #157) |
| `SVC_NETWORK_RECOVERY_IDLE_WINDOW_US` | 15 min | exit after 15 min with no exempted HTTP request |
| `SVC_NETWORK_RECOVERY_ABSOLUTE_CAP_US` | 30 min | hard exit regardless of activity |
| `SVC_NETWORK_COOLDOWN_US` | 10 min | after exit, auth failures are not counted |

- Slow path (PR #157): `s_consecutive_fail` counts disconnects of **any** reason
  under the same `recovery_guards_active()` guard, so a configured clock whose AP
  is off, out of range, or answering `200`/`201` reaches the recovery AP after
  24 failures (~10 min at the capped backoff) instead of retrying forever.
  `200`/`201` are the only non-auth reasons still expected to drive this path:
  `205 CONNECTION_FAIL` is auth-class, because it means the AP was seen and
  rejected the association or handshake (the wrong-password signature on routers
  that do not answer `15`/`202`/`204`).
  Both triggers call `arm_recovery_entry()`, which sets `s_recovery_active`
  before arming the 250 ms one-shot and rolls both flags back if the arm fails.
- An empty SSID in NVS is treated as unconfigured (provisioning AP); an empty
  password with a non-empty SSID is still a valid open network.
- Cold-boot bring-up failure (`bringup_ap_driver()`, `start_ap_mode()` leaving
  `s_ap_mode` false, or the STA `set_mode` / `set_config` / `start` sequence)
  arms the bring-up retry ladder before `svc_network_start()` returns
  `ESP_FAIL`. The failure path leaves `s_started` set so the retry worker's
  `stop()` can `esp_wifi_deinit()` before `start()` runs again.
- Provisioning AP (CR #4): one identity for both entry paths, so nothing over
  the air distinguishes an unconfigured boot from a lockout -- SSID
  `ChronoLink-<MAC4>` (last two STA MAC bytes, 15 chars), WPA2-PSK on channel 1,
  up to 4 clients, 16-char uppercase-hex passphrase from the hardware RNG,
  regenerated on every entry, never persisted and never logged -- except behind
  the bench-only `CHRONOLINK_DEBUG_PRINT_TOKEN` build flag (see next bullet).
  `start_ap_mode()` generates the identity itself (`generate_ap_identity()`),
  under `s_ap_identity_lock`, for both `AP_MODE_SETUP` and `AP_MODE_RECOVERY`,
  and the designated initialiser's default auth mode is `WIFI_AUTH_WPA2_PSK`.
- Bench aid: with `-DCHRONOLINK_DEBUG_PRINT_TOKEN=1` (set in
  `platformio.local.ini` for `env:single_mcu_debug`, git-ignored; the same
  pattern `svc_http.c` uses for the API token) `start_ap_mode()` logs
  ` AP passphrase (bench debug): <pass>` right after the SSID line, because a
  bench board has no panel to show the provisioning screen. With the flag
  undefined the line and its format string are removed by the preprocessor.
- Entry and exit are in place -- `svc_network_stop()` then
  `start_ap_mode()` with recovery set, and the reverse on exit -- so nothing
  reboots and `svc_time` never stops.
- `svc_http`'s static `provisioning_exempt()` waives the token for the
  commissioning endpoints -- `/api/wifi`, `/api/config`, `/api/timetable` and
  `/api/reboot` -- only while the provisioning AP is up. On those paths a
  non-GET request (`POST`, `DELETE`) must also carry
  `Content-Type: application/json`, which an HTML `<form>` cannot produce, so
  form-based CSRF stays blocked; GET is exempt from that requirement because a
  GET must be side-effect free and no browser attaches a content-type to one.
  The token is still accepted on every exempt path. Every write outside that
  list keeps the token (`/api/ota`, `/api/factory-reset`,
  `/api/config/preset`, `/api/timetable/regenerate`), the last two because
  `uri_path_equals()` matches a full path and not its sub-paths.
- Auth model as it stands: **reads are open, writes require auth.** No GET
  handler calls `require_auth()` at all, so `/`, `/api/config`, `/api/timetable`,
  `/api/prayers` and `/debug/ntp` answer without the token in every mode, not
  only in AP mode. The method guard in `provisioning_exempt()` is therefore
  defensive only: it stops a future GET handler on one of the four exempt paths
  from inheriting a content-type requirement that would lock a browser out of
  reading.
- `ui_producer` draws one provisioning screen (SSID + passphrase) for both
  entry paths, keyed on `svc_network_is_ap_mode()` alone.
- Provisioning accessors: `svc_network_is_ap_mode()`,
  `svc_network_get_ap_ssid()`, `svc_network_get_ap_passphrase()`
  (`ESP_ERR_INVALID_ARG` for a too-small buffer, `ESP_ERR_INVALID_STATE` when
  no AP is up or the identity is not ready -- both fail closed rather than
  truncating), and `svc_network_touch_recovery_idle_timeout()` (called by
  `svc_http` on each exempted request; writes a timestamp only, and only while
  the recovery AP -- the one with an idle timer -- is up).
- The security invariants are recorded in `docs/Architecture.md` section 4.3.
  CR #4 inverted the old invariant 1: the exemption now keys on
  `is_ap_mode()`, which is only sound because **every** provisioning AP is
  WPA2-PSK. Never revert an AP to `WIFI_AUTH_OPEN` without reverting the
  exemption in the same change.

---

## Stubbed or placeholder (must not be relied on)

| Item | State |
|---|---|
| MAX7219 display backend | returns `HAL_ERR_DEV`, draws nothing; Kconfig-gated behind `ALLOW_STUB_BACKENDS` |
| Remote display backend | returns `HAL_ERR_DEV`, sends nothing; Kconfig-gated behind `ALLOW_STUB_BACKENDS` |
| `HAL_Audio_SetVolume` | returns `HAL_ERR_DEV`, does nothing |
| `display_set_brightness` | always returns `false` |
| `audio_play_mp3` | always returns `false` |
| ST7796S `write_text` | not implemented in backend. (The backend correctly declines text; the font layer is the sanctioned path for both panel families, mono for 1-bit panels and RGB565 for colour panels.) |
| Theme registry, web config | do not exist (the prayer scheduler landed in PR 5.6 -- see above) |
| SD card | pin defined, no driver |
| Boot manifest, `hw_profile.json`, signature checks | not implemented |
| OTA update service | only partition logging |

---

## Known issues (code)

Verify each against hardware or datasheet before fixing.

1. **VEML7700 lux scale** may be about 16× too low for the default
   gain / integration config. Verify against the datasheet.
2. **I2C frequency** setting is ignored (driver defaults to 400 kHz,
   Kconfig says 100 kHz).
3. **I2C fallback pins** (18/17) are a last-resort runtime bailout used
   only if both BOARD_I2C_* and PDL_PIN_I2C_* resolve to invalid GPIOs.
   The values now match pdl_pins.h, so a bailout lands on the correct
   pads, but the correct long-term behavior is to fail cleanly instead
   of silently substituting pins. Audio fallbacks were removed in PR 3
   (they were dead -- the HAL audio driver reads pdl_pins.h directly).
4. **Backlight** is switched on in `pdl_board_init`, before the panel
   is initialised.
5. **SD_CS (IO9)** is never driven high, so it may float on the
   shared SPI bus.
6. **ST7796S backend** bypasses the `HAL_SPI` mutex and manages DC
   itself.
7. **Sensors:** failed probe is permanent; `HAL_Sensors_Init` always
   returns OK; SHT4x CRC not checked.
8. **I2C** uses the legacy driver with deprecation checks
    suppressed, and lazy init is not under the lock. No bus recovery.
9. **Audio:** stop/abort is cooperative only -- `HAL_Audio_Stop()` sets a
    flag that `HAL_Audio_Play()` polls between 1024-sample chunks (~23 ms
    at 44.1 kHz) -- and nothing calls it yet. `Play()` has no lock against
    `Stop()`/`Deinit()` on another task.
10. **Display router:** custom SDK driver unreachable when a built-in
    backend is selected; mock replaces rather than adds; lock has no
    timeout.
11. **HAL failure policy:** a display or SPI init failure aborts the
    whole boot. Consider degrading instead (the clock should keep
    time).
12. **Build hygiene:** `IDF_PATH` include hacks in the HAL CMake,
    global `-I` in `platformio.ini`, coredump off.
13. **main/Kconfig.projbuild `CONFIG_BOARD_AUDIO_*` entries are dead.**
    BOARD_AUDIO_I2S_BCLK, BOARD_AUDIO_I2S_WS, BOARD_AUDIO_I2S_DOUT,
    and BOARD_AUDIO_SD_MODE are not read by any driver. The HAL audio
    driver reads pins from pdl_pins.h (PDL_PIN_I2S_*), not from these
    Kconfig entries. Leave the entries in place for now (removing them
    changes the menuconfig menu tree and would need a migration note);
    either delete them or wire them into the HAL in a future PR.
14. **NTP needs `CONFIG_LWIP_SNTP_MAX_SERVERS=3`.** `svc_time` configures
    three servers (`time`/`ntp_server_0..2`); LwIP's SNTP keeps only
    `CONFIG_LWIP_SNTP_MAX_SERVERS` of them (default `1`) and drops the
    rest. Added to `sdkconfig.defaults` by PR 5b-2, but a stale
    `sdkconfig.single_mcu` silently keeps `1`; `svc_time` logs the clamp.
15. **The `svc_time` tick relies on picolibc's `time()` / `localtime_r`**
    (via the HRT systimer). DS3231M drift is not corrected during a boot
    unless NTP resyncs; the RTC is re-read every tick when present. A
    resync that IDF runs on its own 1 h cycle
    (`CONFIG_LWIP_SNTP_UPDATE_DELAY`) steps the system clock without
    updating the RTC, so the snapshot follows the RTC and does not see it;
    subscribe to `NETIF_SNTP_TIME_SYNC` or prefer `time()` if that matters.
16. **HIL function names are unprefixed** (`rtc_*`, `i2c_*`, `spi_*`,
    `gpio_*`, `display_*`, `audio_*`, `sensor_*`) and can collide with
    IDF internal symbols when first called. `rtc_init` collided with
    `esp_hw_support`'s `rtc_init` in PR 5b-2 and was renamed to
    `hil_rtc_init`. Rename the others with a `hil_` prefix when they are
    first called.
17. **`DM_MAX_TEXT_LEN` was 8, which left no room for a NUL** on an 8-char
    string -- `"HH:MM:SS"` is exactly 8 -- so `font_engine_draw_text()`
    walked past the end of `arg.text.text` into the union and the next
    command. Latent, not theoretical: no caller of
    `display_manager_submit_frame()` existed until PR 6. Fixed in PR 6:
    the macro was bumped to 16. **Bumped again to 32 in PR 5.5b** to
    accommodate the time-invalid banner text. Termination is still
    guaranteed (`dm_cmd_draw_text()` caps the copy at `DM_MAX_TEXT_LEN - 1`
    and always writes the NUL).
18. **`display_manager/README.md` was stale** -- it claimed a lock-free
    queue and "8 slots x 512 bytes"; the code has a submit mutex and
    `DM_FRAME_QUEUE_DEPTH` 3 slots of `sizeof(dm_frame_t)`. Corrected in
    PR 6.
19. **S1 ❌ WON'T FIX** -- `parse_file_at()`
    (`components/svc_timetable/src/svc_timetable.c:222`) uses an
    unbounded `strstr(buf, "\"days\"")` to find the day array. It is
    theoretically fragile (a `"days"` literal inside an earlier value
    could match first) but has no demonstrated real-world failure path:
    a wrong match makes the bracket lookup fail and returns
    `ESP_ERR_INVALID_RESPONSE`, rejecting the upload. No code change.

**Resolved**

- **Event bus had no consumer** (found 2026-10-11, fixed 2026-10-14 by
  PR 5a). Every `event_bus_publish()` landed in the 32-slot kernel event
  queue with no reader, so the queue filled in ~32 s and the
  evict-oldest path in `event_bus_publish()` discarded entries --
  typically the 1 Hz `EVENT_BOOT_HEALTH`. Observed on hardware as a
  climbing drop count (400+ drops in 7 minutes). The kernel-owned
  `event_dispatch_task`, created first in `core_os_start()`, now drains
  the queue; subscriber fan-out followed in PR 5b-1 and 5b-2.

- **`svc_time` served a frozen snapshot** (found 2026-10-14, fixed by PR 5c,
  together with the "default TZ is UTC" gap). The snapshot was captured once
  per sync and re-printed unchanged until the next event; the worker now
  ticks it at 1 Hz (RTC when present, otherwise the libc clock after an NTP
  sync), and the default TZ is the UK POSIX string instead of `GMT0`.

- **`svc_time` published spurious `EVENT_TIME_DATE_CHANGED` at |offset| ≥ 10 h.**
  The offset detector used a ±14 h window, flipping between two values twice
  a day for eastern Australia, New Zealand, Hawaii, etc. Fixed: the offset is
  normalised modulo 24 h, so it is constant per real offset and still changes
  by 3600 s on a DST step.

- **Jafaria Isha could land before Maghrib at ~60°N.** The high-latitude
  clamp measured the Isha portion from `sunset`, but Jafari `maghrib` is the
  4° angle which comes later. Fixed: measured from `maghrib`.

- **WiFi credentials were silently truncated.** `strncpy(..., sizeof(field)-1)`
  cut a 32-char SSID to 31 and a 64-char hex PSK to 63. Fixed: `memcpy` of
  `strnlen(..., sizeof(field))`, which copies the full field when full and
  leaves shorter values NUL-terminated (`wcfg` is memset first). NVS
  encryption remains a separate hardening item.

- **`HAL_GPIO_Init`** still configures GPIO2 as an output (leftover
   example). Mask-versus-index handling in `hal_gpio_safe.c` is
   ambiguous, and `GPIO_PIN_COUNT` 48 rejects GPIO48.
    - *Status: Resolved in code (Doc updated during HIL OTA verification).*

- **DST Rollover Verified on Hardware (Oct 25, 2026):** The POSIX TZ string (`GMT0BST,M3.5.0/1,M10.5.0`) correctly triggered the clock to fall back from `01:59:59` (BST) to `01:00:00` (GMT). The prayer engine correctly pre-calculated for the offset change at midnight, preventing any mid-day time jumps. OTA rollback was also verified on hardware.

- **Scheduler marker could regress on dropped rearm (CR #6, 2026-11-15):** the "too late" branch in `scheduler_evaluate()` set `last_fired_epoch[p] = epoch` unconditionally. If a config change moved a prayer earlier and then back within 2 minutes, and BOTH `EVENT_PRAYER_TIMES_UPDATED` events were dropped (bus full), the marker could regress and the prayer could double-fire on the next legitimate fire. Fixed: the marker only ever advances. Regression test in `test_scheduler::test_marker_never_regresses`.

- **Failed RTC write could revert NTP time (CR #5, 2026-11-15):** if `hil_rtc_set_time()` reported success but the write did not land (I2C glitch, partial BCD write, misbehaving RTC), `tick_snapshot()` would then prefer the stale RTC over the fresh NTP snapshot within one second, silently reverting the clock. The hourly RTC correction would then push the system clock back too. Fixed: the RTC write is now verified by reading it back; a `s_rtc_write_failed` file-scope flag is set on any write that does not read back correctly, and both `tick_snapshot()` and `run_hourly_rtc_correction()` skip RTC while it is set. Cleared only on a verified successful write. `rtc_roughly_equal()` tolerates 1 s drift to allow for the second boundary rolling during write+readback. The comparison is now UTC-epoch-based, so a rollover of the minute, hour, day or year during write+readback also compares equal (the earlier field-by-field check did not), and an invalid date fails closed because `utc_to_epoch()` returns false. Note: the flag's declaration comment says "Guarded by `s_state_lock`"; writers (in `do_ntp_sync`) and the `tick_snapshot` reader are under the lock, but the `run_hourly_rtc_correction` read is not. On Xtensa single-byte access is atomic, so the race is benign (worst case: one hour-key check reads the pre-sync value). Flagged for a future comment correction or wrap-in-lock fix.

- **Silent missed adhan on a full event bus (CR #1, 2026-11-17):**
  `svc_scheduler.c` recorded the prayer as fired (inside
  `scheduler_evaluate()`) before `event_bus_publish()`, and that function
  returned void. If the 32-slot bus was full, the evict-oldest path could
  discard the `EVENT_PRAYER_TIME_REACHED` and there was no retry -- the
  adhan silently never played. Fixed: `event_bus_publish_checked()` returns
  `bool` (existing `event_bus_publish()` is now a `(void)` wrapper, so all
  other callers are unchanged). `scheduler_tick()` snapshots
  `last_fired_epoch[]` and `pending_isha_epoch` before `scheduler_evaluate()`
  and, on a failed publish, rolls back the affected marker so the next 1 Hz
  tick retries. A failed pending Isha also has `pending_isha_epoch`
  restored, since `scheduler_evaluate()` had already consumed it. Bounded
  by `SCHEDULER_MAX_LATENESS_S` (120 s); beyond that the prayer is missed,
  as with any stall. Compile-verified only (the path fires only when the bus
  fills); `scheduler_logic.c` and its host tests are unchanged.

- **Dead `src/` scaffold deleted (CR S2, 2026-11-18):** the repo-root
  `src/` tree held unused placeholders (`src/app/*`, `src/svc/*`) plus a
  stray `void app_main() {}`. It was never built: `platformio.ini` sets
  `src_dir = main`, nothing registers `src/` as a component, there is no
  `src/` entry in `.pio/build/single_mcu/build.ninja`, and
  `EXTRA_COMPONENT_DIRS` is empty -- so `src/CMakeLists.txt` and its
  `src/*.*` glob were never evaluated. The `src/svc/svc_scheduler.c`
  deletion in PR 5.6 removed one file from the same dead tree; this
  removes the rest.

- **Safe mode runs a heartbeat with no dispatcher (CR S3, 2026-11-18):**
  documented as **unreachable dead code; no firmware change required**.
  Nothing sets `BOOT_FLAG_SAFE_MODE`: `main.c` passes 0,
  `read_pending_boot_request()` can only return
  `BOOT_FLAG_FACTORY_RESET`, and `app_main()` starts every service
  regardless. If the path were ever entered, `core_os_init()` has already
  started the 1 Hz `EVENT_BOOT_HEALTH` timer before the safe-mode check
  and `core_os_start()` returns before `event_dispatch_task` exists, so
  the 32-slot bus fills and evicts oldest (the CR #1 class); `core1_task`
  is also skipped, so submitted frames never render. Wiring it up is a
  separate design item -- if done, start the dispatcher before the
  safe-mode early return.

- **`hours_to_sec_common()` returned 0 (= midnight) for NaN (CR S4,
  2026-11-18):** the helper converted a NaN hour to `0`, which reads as a
  legitimate 00:00:00 -- a failed high-latitude computation would have
  been displayed as midnight rather than "unavailable". Fixed: NaN now
  returns `UINT32_MAX`, the sentinel `asr_yellow_sec` already used and
  which the jamat tier treats as DISABLED (`svc_http` emits JSON `null`).
  **V6 re-verdict: still unreachable.** All 11 call sites receive a
  `PrayerTimes::calculate()` result that passed the validity gate
  (`PrayerTimes.cpp:415-418`; otherwise `ESP_ERR_INVALID_STATE` at
  `svc_prayer_engine.cpp:145-147`), so fajr/sunrise/dhuhr/asr/maghrib/
  isha are never NaN; `sunset` is covered by the polar bail at
  `PrayerTimes.cpp:264`; `zawal` is pure transit arithmetic and
  `midnight` (lines 385-391) is built only from sunset/sunrise/fajr,
  all non-NaN; `asr_yellowing` was already sentinel-mapped explicitly at
  `svc_prayer_engine.cpp:156-158`. No field besides `asr_yellow_sec` can
  therefore carry the sentinel, and the only unchecked arithmetic in
  `jamat_calculator.c` (`ADHAN_PLUS`, line 54) takes an anchor derived
  from the NaN-gated fields, so it cannot wrap. No caller guard added.
  Compile-verified only: the helper is `static` and no public-API input
  produces NaN, so a host test would require exporting it -- not done.

- **`event_init_payload()` skipped `type` on an empty payload (CR S5,
  2026-11-18):** the early return ran before `evt->type = type`, so a
  caller passing `payload = NULL` / `payload_len = 0` published an event
  whose type was whatever the stack held. Fixed: the `evt == NULL` guard
  stays first, `type` is assigned next, and only a real payload is
  copied; the payload region is zeroed before the copy so bytes beyond
  `payload_len` are not stale stack contents forwarded to subscribers.
  No current caller passes NULL/0 (`boot_manager` x2, `core_os` and
  `svc_scheduler` each pass a full struct; `svc_network` sets the type
  itself and its workaround comment was updated), so this is
  latent-bug hardening. Build-verified only -- adding FreeRTOS stub
  headers to `prayer_host_tests` for a `static inline` in a header was
  judged not worth it.

---

## Known issues (architecture)

See [Architecture.md](Architecture.md) §5.

Additional issues:

- Split pin sources: `pdl_pins.h`, two Kconfig files, unused
  `config/pins.example.json`.
- `docs/partitions.csv` was out of sync with root until the
  2026-10-04 docs pass; must stay identical.

---

## Open decisions (need an owner)

1. **Module variant:** N16R2 or N16R8? Affects PSRAM and GPIO 35–37
   usage. Currently PSRAM is off in `sdkconfig`.
2. **Pin source of truth:** `pdl_pins.h` only, or Kconfig-overridable?
3. **Service access to display:** allow services to use the display
   HAL directly, or add a HIL graphics adapter?
4. **Degradation policy:** which devices are mandatory vs optional?
5. **Schematic check of pins:** the retired `pinout.csv` showed likely
   errors (UART0 on IO36/37, JTAG TMS/TDO swapped, SD_MODE on IO1
   while code says unused, backlight mismatch). Regenerate from the
   schematic.

---

## Planned boot work (not started)

### Factory partition: not planned (decision 2026-10-11)

The 16 MB flash is fully allocated: two 4 MB OTA slots (`app0` / `app1`),
NVS, `otadata`, `phy_init`, and ~7.9 MB FATFS at `/storage`. Adding a
`factory` partition would require shrinking the OTA slots (reducing the
maximum OTA image size) or `/storage`, and no concrete failure mode has
been identified that justifies the cost.

This matches Espressif's own guidance: the `factory` partition is
optional in an OTA project. Two OTA slots and `otadata` are sufficient.

**Recovery model:**

| Failure mode | Recovery |
|---|---|
| Healthy device, new firmware available | OTA update into the inactive slot |
| OTA wrote a bad image | App rollback reverts to the previous slot (see `docs/boot.md` §2) |
| Both slots unbootable (e.g. power loss mid-OTA) | USB reflash — accepted limitation; USB is exposed |
| User wants to start over | Data reset: `BOOT_FLAG_FACTORY_RESET` wipes NVS + `/storage`, reboots |

**Terminology — four distinct mechanisms, four different names.** Do not
use "factory reset" as a catch-all; it means different things to different
readers.

| Name | What it does | Status |
|---|---|---|
| `BOOT_FLAG_FACTORY_RESET` → `do_factory_reset()` | Wipe NVS + erase `/storage` partition (reformatted on next boot), reboot. **Not an image reset.** | Exists in `boot_manager.c`; producers: caller-supplied flag, `POST /api/factory-reset` (PR 4.5). #129 (R3): erase via `esp_partition_erase_range()` so a cleanly-mountable FS is still wiped |
| `factory` partition | Known-good golden image slot | Not present, **not planned** |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | Auto-revert a bad OTA to the previous slot | **Enabled** (PR 4.6, extended 2026-11-15); `main.c` calls `esp_ota_mark_app_valid_cancel_rollback()` at the end of `app_main()`, after all services start (#129 R2). **(CR #3, 2026-11-15)** Gated on `svc_http_start()`: if it fails, `esp_ota_mark_app_invalid_rollback_and_reboot()` runs instead, since a firmware with no HTTP server has no recovery path |
| USB reflash | Manual recovery via `esptool.py` | Always available |

### Other planned boot work

- **Full factory reset:** current data reset wipes NVS + `/storage` only.
  Does not touch `otadata`, does not force a firmware revert, does not
  clear `phy_init`. Tracked as three separate future PRs.
- **Boot manifest** (hardware-profile hash, signature checks) and
  `hw_profile.json`. Not implemented.
- **Safe-mode trigger and policy** beyond "skip service tasks".
- **Boot-failure events** before the event bus exists.

---

## Web / config work (in progress)

The design intent is that all user configuration goes through a web UI.

- **`svc_http` (PR 4.1):** HTTP server on port 80 with JSON REST APIs.
- **AP Setup Mode (PR 4.2; #131 removed fallback; PR #157 empty-SSID):** If no WiFi credentials are saved **or the saved SSID is empty**, `svc_network` drops into Access Point mode (`ChronoLink-<MAC4>`, WPA2-PSK, IP `192.168.4.1`). An empty SSID can never associate, so it is treated as unconfigured rather than fed to the STA ladder; an empty password with a non-empty SSID stays a valid open network. `ui_producer` displays these setup instructions on the screen instead of the clock. **(CR #2, implemented — hardware verification checklist in the design doc is still open; see `docs/sessions/cr2-wifi-lockout-design.md`; CR #4 unified both APs into one)** The same provisioning AP is also entered after `SVC_NETWORK_AUTH_FAIL_THRESHOLD` (5) consecutive auth-class disconnects (reason 2/15/202/204/205) with no guard active, **or after `SVC_NETWORK_UNREACHABLE_FAIL_THRESHOLD` (24) consecutive disconnects of any reason** (PR #157 slow path, so a router that is off or out of range also ends in a reachable AP). Implemented in `svc_network`: the state machine, the three workers, the four timers (entry trigger, idle/absolute timeout, bring-up retry, cooldown), the bring-up retry ladder, and the accessors `svc_network_is_ap_mode()`, `svc_network_get_ap_ssid()`, `svc_network_get_ap_passphrase()`, `svc_network_touch_recovery_idle_timeout()`. **httpd exemption implemented (step 2, widened by CR #4):** `svc_http.c` has a static `provisioning_exempt()`, called first from `require_auth()`. While the provisioning AP is up it lets `/api/wifi`, `/api/config`, `/api/timetable` and `/api/reboot` through without the `X-Chronolink-Auth` token; on those paths a non-GET request must also carry a `Content-Type` of `application/json` (strict, case-sensitive; a `<form>` cannot produce it, so form-based CSRF stays blocked), and an exempt request then touches the recovery idle timer. GET is exempt from the content-type requirement because a GET must be side-effect free and no browser attaches one. **Reads are open, writes require auth:** no GET handler calls `require_auth()` at all, so `/`, `/api/config`, `/api/timetable`, `/api/prayers` and `/debug/ntp` answer without the token in every mode, not only in AP mode — the method guard in `provisioning_exempt()` is defensive only. Every write outside that list — `/api/ota`, `/api/factory-reset`, `/api/config/preset`, `/api/timetable/regenerate` — still requires the token (the two sub-paths are not covered: `uri_path_equals()` matches a full path only), and no AP mode means no exemption at all. **Timing:** the recovery AP exits after 15 min idle / 30 min absolute, then observes a 10 min cooldown during which auth failures are not counted. **Provisioning screen (step 3, unified by CR #4):** `ui_producer` has a single `svc_network_is_ap_mode()` branch — there is no second predicate any more, because both entry paths share one identity. It draws a 9-command screen (clear, then `WiFi Provisioning Mode`, `Connect to:` + SSID, `Password:` + passphrase, `Open Browser:` + `192.168.4.1`, `Auto-exits when idle`). The identity is re-read every 250 ms through `svc_network_get_ap_ssid()` / `svc_network_get_ap_passphrase()` and folded into the draw key `AP_<ssid>_<passphrase>`, so a rotation on re-entry forces a redraw; a failed getter — AP mode ended between the predicate and the read — skips the frame rather than drawing stale or partial strings, and the key is only stored after a successful submit. Leaving AP mode re-runs the full clear-and-redraw path, so none of the AP lines survive under the clock. The passphrase is still never logged and never exposed over HTTP (the single exception is the bench-only `CHRONOLINK_DEBUG_PRINT_TOKEN` build flag described above, which is absent from shipped images), and no API token has to be shown on the panel any more: the exemption covers every commissioning endpoint, so nothing now remains between a lockout and a working setup session.
- **Web UI (PR 4.3, extended PR 5.1b):** Embedded HTML portal (`components/svc_http/www/index.html`) served at `GET /`. Provides forms for WiFi, Location, Calculation Method, live Prayer Times (`GET /api/prayers`), and a Factory Reset button. Includes reboot (`POST /api/reboot`) and factory-reset (`POST /api/factory-reset`) endpoints. **PR 5.1b additions:** editable jamat configuration — adhan safety margins per prayer, seasonal jamat table (5 prayers × winter/summer anchor + offset + rounding), season transition dates, and a preset selector that calls `POST /api/config/preset`. Full prayer-times configuration is now settable from the browser without
recompiling. **Hijri calendar:** the config card carries a Hijri method
dropdown (`hijri_calc_method`: Umm al-Qura KSA / Kuwaiti tabular) and a
Hijri offset dropdown (`hijri_offset_days`: -3 to +3 days), so a mosque's
announced Ramadan start can be matched from the browser.
- **Factory-reset trigger (PR 4.5):** `POST /api/factory-reset` writes a one-shot flag to NVS namespace `boot_req` (key `flag`) and reboots. `boot_manager_init()` reads and clears that flag immediately after `init_nvs()` and runs the existing `do_factory_reset()` (NVS erase + /storage partition
  erase + reboot; #129 R3 erases the raw partition so a cleanly-mountable
  FS is still wiped). The caller-supplied `BOOT_FLAG_FACTORY_RESET` parameter path is kept for devices with unreadable NVS.
- **Firmware update over HTTP (PR 4.7):** `POST /api/ota` accepts a raw firmware image (`application/octet-stream`), requires the `X-OTA-Token` header, streams it in 4 KB chunks to the inactive OTA slot, and reboots into it on success. Compile-time gate: `#error` if `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is not set. Failure at any stage calls `esp_ota_abort()` and leaves the running image untouched.
- **Multi-language display (planned):** the numeric panel format
  (`18/05/1447`) is language-agnostic and does not require non-English
  month bitmaps. Multi-language support (EN/AR/UR) will add a
  `svc_config.ui_language` field, a `POST /api/config` field, and a web
  UI language selector; the web UI switches languages via browser-side
  locale-aware formatting of the existing `hijri.month` numeric. Panel
  display stays numeric in every language. Design contract documented in
  `prayer-times-model.md` §10.1 (format specification) and §11
  (out-of-scope table).

---

## Planned HAL work (not started)

- Health / diagnostics registry with a callback the kernel turns into
  events.
- Runtime device presence API.
- Finer status codes.
- SPI device-handle API with DC handling; move the ST7796S onto it.
- Backlight PWM and ambient-light auto-dim.
- Button / input HAL (for future input beyond the web UI).
- RTC extras (weekday, temperature).
- Audio stop / mute / streaming.
- SD driver.
- Reset-reason and heap info.
- Move to the new I2C driver with bus recovery.
- Host-side tests with an I2C mock.
