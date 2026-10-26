# Project Status

Snapshot reviewed 2026-10-24 at commit `cfdc9ce` (PR: svc_http REST APIs, AP fallback, web UI, factory reset, app rollback, OTA upload). Update this file in the same change that alters any item below.

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
- **App rollback (PR 4.6):** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`;
  `boot_manager_init()` calls `esp_ota_mark_app_valid_cancel_rollback()`
  after `core_os_start()` returns, so a freshly-OTA'd image that reaches
  the handoff stage marks itself valid. An image that crashes or resets
  before that point is reverted to the previous slot on the next reset.
- **OTA over HTTP (PR 4.7):** `POST /api/ota` in `svc_http` streams a
  raw firmware image (`application/octet-stream`) to the inactive OTA
  slot via `esp_ota_begin` / `esp_ota_write` / `esp_ota_end` /
  `esp_ota_set_boot_partition`, then reboots. Guarded by a compile-time
  `X-OTA-Token` shared secret (`OTA_SHARED_TOKEN`) and a build-time
  `#error` if `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is not set. On any
  failure calls `esp_ota_abort()` and leaves the running image
  untouched.
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
- **Services:** `svc_network` (WiFi station), `state_manager`, `svc_config` (NVS-backed installation config: location, method, madhab, iqamah offsets, adhan mode, display preferences, high-latitude rule, adhan offsets, zawal buffer — storage + accessors only, no calculation or hardware). **PR 5.1a additions:** seasonal jamat config (`jamat[5]` with per-prayer winter/summer anchor+offset and shared rounding, plus a `season` struct with fixed summer/winter transition dates) and named presets (`SVC_CONFIG_PRESET_UK` default, `SVC_CONFIG_PRESET_PAKISTAN`). Legacy `iqamah_offset_min[]` retained as deprecated mirror of `jamat[i].winter.offset_min` for backward compatibility (removal in 5.1c; `refactor(config): drop iqamah mirror`).
- **`svc_prayer_engine` (PR 13):** pure C++ prayer time calculation. Standalone library in `components/svc_prayer_engine/include/prayertimes/` (no ESP-IDF dependency; usable outside this project). Computes Fajr, Sunrise, Dhuhr, Zawal Start, Asr, Asr Yellowing (preferred end), Maghrib, Sunset (true astronomical), Isha, Islamic Midnight for a given date, coordinates, timezone offset, and method (MWL, ISNA, Egyptian, Karachi, Umm al-Qura, Jafaria, Moonsighting Committee, Custom Angles). High-latitude fallback supports AUTO (seventh-of-night above 48° latitude, middle-of-night at or below 48°), plus explicit NONE, MIDDLE_NIGHT, SEVENTH_OF_THE_NIGHT, and ANGLE_BASED rules selectable via the wrapper. Default is AUTO. **Verified against AlAdhan API** across four global cities (Horsham, Karachi, Makkah, New York) — Fajr/Sunrise/Dhuhr/Maghrib/Isha within ±30 s after PR 19. Asr is intentionally *not* compared to AlAdhan: AlAdhan samples solar declination at noon, while this engine samples at the Asr instant, so the post-PR-19 Asr differs from AlAdhan by up to ~100 s in favour of the more accurate iterated declination. Wrapper API: `svc_prayer_engine_calculate()`. The high-latitude fallback uses clamping (PrayTimes.org style) rather than NaN-only. AUTO selects one-seventh-of-night whenever |lat| > 48° or any twilight angle the method uses is unreachable at the summer solstice (|lat| > 66.5 − angle). The unreachable-angle branch covers Egyptian from ~47.0°N — the exact-48° boundary is no longer special-cased. Middle-of-night is used otherwise (Middle East, South Asia). Both branches depend only on latitude + method, so the rule cannot flip mid-year.

  Islamic midnight is max(sunset→clamped-Fajr midpoint, Isha) — never earlier than Isha, and continuous day-to-day (the old "fall back to a sunrise-based definition when midnight < isha" jumped by up to 1.4 h near the clamp boundary). Isha and midnight are computed as offsets from the wrapped maghrib/sunset values, so the maghrib→isha and sunset→midnight intervals stay correct even when civil time is far from solar time (western Alaska, Jafaria at ~60°N). (PR 18) Jafaria method reachable: `svc_config_method_t` extended to `JAFARIA = 5, COUNT = 6`, matching `svc_prayer_method_t` 1:1. Date validation rejects non-existent days (e.g. 31 Feb, 29 Feb in non-leap years). `asr_yellow_sec == UINT32_MAX` when the sun never reaches 5° above the horizon (high-latitude winter); distinct from `valid == false`. On error, the API returns without writing `*out` — callers must check the return code before reading fields. (PR 19) Sun-position sampling is now iterated per event rather than fixed at 0h UT: `mid` is converged once, then each angle event and Asr is iterated 2-3× at `jd − lon/360 + t/24` until the declination sample stabilises (<1 s residual). This closes the equinox accuracy gap Claude's PyEphem review measured at ±125 s, verified on hardware 2026-10-17: Fajr/Sunrise/Dhuhr/Asr/Maghrib/Isha deltas vs the 0h-UT baseline exactly match the Python `mode2` prediction (+15 / +25 / −6 / −59 / −88 / −92 s).
- **`svc_prayer_engine_service` (PR 15, extended PR 5.1a):** runtime service that reads `svc_config` (location, method, madhab, high-latitude rule, adhan offsets, zawal buffer) and `svc_time` (local date), calls `svc_prayer_engine_calculate()`, applies per-prayer adhan offsets (`adhan_offset_min[]`), computes zawal start (`dhuhr - zawal_buffer_min`), and publishes `EVENT_PRAYER_TIMES_UPDATED`. **PR 5.1a additions:** publishes a three-tier snapshot — `astronomical_*` (astronomical calculation + timezone, no user offsets), `*_sec` (adhan = astronomical + `adhan_offset_min[]`), `jamat_sec[]` (seasonal jamat, with per-prayer rounding) — plus auxiliary times (`sunrise_sec`, `sunset_sec`, `midnight_sec`, `zawal_start_sec`, `asr_yellow_sec`), Ramadan endpoints (`sehri_end_sec`, `iftar_sec`), and `jamat_tomorrow_sec[]` for the "tomorrow changes" notification. Consumers read finished values; no offset math in the UI. Public API: `svc_prayer_engine_service_start()`, `_recalculate()`, `_get_times()`. The event is a signal only (payload truncated to 32 bytes); consumers must call `_get_times()` for the full snapshot. Recalculation is triggered by `EVENT_TIME_UPDATE` (successful NTP sync) and by `EVENT_TIME_DATE_CHANGED` (local date or UTC offset change). (PR 17) Rolls over at midnight and at DST transitions: `svc_time` publishes `EVENT_TIME_DATE_CHANGED` when the local calendar date or the effective UTC offset changes; the service subscribes, recalculates, and self-heals within 60 s if the event is dropped. Timezone offset is taken at **noon of the target date** (not at call time), so a DST-transition day's prayer times all use the same offset. Design intent for the whole prayer-times model is in [prayer-times-model.md](prayer-times-model.md).
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
  submits a 3-command `dm_frame_t` (clear / fill_rect / draw_text) when the
  formatted local `HH:MM:SS` changes. The full-screen clear is first-frame
  only; later frames are fill_rect + draw_text. The old placeholder (which
  only called `display_manager_init()`) is gone.
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
- **Known IDF limitation (IDFGH-18398):** on a timeout SNTP may retry the
  same server instead of advancing to the next index, so the three-slot
  fallback is **best-effort, not a guarantee**. Live in the code: the sync
  path hands the servers over in slot order and does not work around it.

The `hal_rtc.h` timezone contract comment records the same decision at
the HAL boundary, so a future contributor editing the driver cannot
add local-time or DST logic there without contradicting a written
contract.

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
| `src/app/*`, `src/svc/*` | empty placeholders; **not built** (`platformio.ini` sets `src_dir = main`) |
| Scheduler, theme registry, web config | do not exist |
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
    the macro is 16 with guaranteed termination (`dm_cmd_draw_text()` caps
    the copy at `DM_MAX_TEXT_LEN - 1` and always writes the NUL).
18. **`display_manager/README.md` was stale** -- it claimed a lock-free
    queue and "8 slots x 512 bytes"; the code has a submit mutex and
    `DM_FRAME_QUEUE_DEPTH` 3 slots of `sizeof(dm_frame_t)`. Corrected in
    PR 6.

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
| `BOOT_FLAG_FACTORY_RESET` → `do_factory_reset()` | Wipe NVS + reformat `/storage`, reboot. **Not an image reset.** | Exists in `boot_manager.c`; producers: caller-supplied flag, `POST /api/factory-reset` (PR 4.5) |
| `factory` partition | Known-good golden image slot | Not present, **not planned** |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | Auto-revert a bad OTA to the previous slot | **Enabled** (PR 4.6); `boot_manager` calls `esp_ota_mark_app_valid_cancel_rollback()` after `core_os_start()` |
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
- **AP Fallback Mode (PR 4.2):** If no WiFi credentials are saved, or if connection fails 3 times, `svc_network` drops into Access Point mode (`ChronoLink-Setup`, IP `192.168.4.1`). `ui_producer` displays these setup instructions on the screen instead of the clock.
- **Web UI (PR 4.3, extended PR 5.1b):** Embedded HTML portal (`components/svc_http/www/index.html`) served at `GET /`. Provides forms for WiFi, Location, Calculation Method, live Prayer Times (`GET /api/prayers`), and a Factory Reset button. Includes reboot (`POST /api/reboot`) and factory-reset (`POST /api/factory-reset`) endpoints. **PR 5.1b additions:** editable jamat configuration — adhan safety margins per prayer, seasonal jamat table (5 prayers × winter/summer anchor + offset + rounding), season transition dates, and a preset selector that calls `POST /api/config/preset`. Full prayer-times configuration is now settable from the browser without recompiling.
- **Factory-reset trigger (PR 4.5):** `POST /api/factory-reset` writes a one-shot flag to NVS namespace `boot_req` (key `flag`) and reboots. `boot_manager_init()` reads and clears that flag immediately after `init_nvs()` and runs the existing `do_factory_reset()` (NVS erase + FATFS reformat + reboot). The caller-supplied `BOOT_FLAG_FACTORY_RESET` parameter path is kept for devices with unreadable NVS.
- **Firmware update over HTTP (PR 4.7):** `POST /api/ota` accepts a raw firmware image (`application/octet-stream`), requires the `X-OTA-Token` header, streams it in 4 KB chunks to the inactive OTA slot, and reboots into it on success. Compile-time gate: `#error` if `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is not set. Failure at any stage calls `esp_ota_abort()` and leaves the running image untouched.

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
