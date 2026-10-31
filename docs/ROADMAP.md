# Roadmap

Forward-looking view of ChronoLink V4 OS: where the firmware stands today
and how the remaining work is sequenced. This document is deliberately
short -- it is a map, not a specification. `STATUS.md` remains the
authoritative record of what is implemented, stubbed or planned at the
current commit and wins wherever the two disagree; `ROADMAP.md` exists so a
reader (or a fresh session) can orient without reading the whole history or
inferring intent from `STATUS.md` fragments. Phases are ordered by
dependency, not by promise of a date.

---

## Current state (2026-10-30)

| Area | State |
|---|---|
| Boot | Firmware boots on the ESP32-S3 devkit; every soft-fail behaves as documented (audio, RTC absent, sensor probes). |
| Network | WiFi station connects and gets an IP; on the guest network NTP is reachable and a sync completes. |
| Event bus | `event_dispatch_task` drains the kernel queue; `event_bus_subscribe()` fans out per type (8 subscriber slots). |
| `svc_time` | First event-bus subscriber. NTP syncs, the DS3231M write path soft-fails on a bare devkit, POSIX TZ (default UK `GMT0BST,M3.5.0/1,M10.5.0`), 1 Hz tick on the snapshot, publishes `EVENT_TIME_UPDATE`. |
| `ui_producer` | 250 ms poll task on core 0; builds a single-string `dm_frame_t` (one `draw_text_scaled` for the clock, plus an optional 3-command red banner when RTC is untrusted) only when the rendered string changes. First frame adds a one-time clear. |
| `display_manager` | Frame queue plus frame counter (`display_manager_frames_dispatched()`). Frames dispatch at 1 Hz on the devkit and the counter advances -- verified on hardware against the PR 6 boot log. |
| Panel | None wired. The ST7796S HAL initialises, RDDID returns all-zero (non-fatal by design), frames write into the void: no crash, no log spam. |
| CI | Two jobs (`PlatformIO build (single_mcu)`, then `ESP-IDF build (esp32s3)`). Artifacts upload only on tag pushes and manual dispatch (`retention-days: 7`), so ordinary push/PR runs leave no artifact storage behind. |

Components that exist today: `boot_manager`, `core_os`, `event_bus`,
`chronolink_hal`, `hil`, `chronolink_driver_sdk`, `state_manager`,
`svc_network`, `svc_time`, `svc_config`, `svc_prayer_engine`,
`svc_prayer_engine_service`, `svc_timetable`, `svc_scheduler`,
`svc_http`, `remote_bridge`, `display_manager`, `ui_producer`,
`font_engine`, `font`, `audio_test`. Phase 2 (prayer engine + scheduler)
and Phase 4 are complete; Phase 3 (adhan playback), Phase 5 (OTA
pull-based) and Phase 8 (polish) remain, with Phase 6 (DS3231 INT wake)
and Phase 7 (per-prayer DST offset) listed as follow-ups.

---

## Phase 1 — Make it visible (in progress)

Goal: pixels on a real ST7796S panel.

- Wire the panel to the devkit, or wait for the assembled PCB.
- Verify the frame path renders. No code change expected: the queue, the
  command set and the GFX path are already exercised end to end.
- Clock face design: PR 8 landed the scalable RGB565 font pipeline
  (`HAL_GFX_BlitScaled`, `FONT_FORMAT_RGB565` in `font_engine`,
  `DM_CMD_DRAW_TEXT_SCALED`, `FONT_UI_SANS` placeholder). Digit size
  and layout are panel-derived. Full-panel visual verification waits
  on the ST7796S panel being wired to the devkit, and on the real
  Inter SemiBold TTF replacing the placeholder.
- Deliverable: the wall clock shows `H:MM AM/PM` in a font readable from
  across the room.
- **PR 5.5 (Timetable) & 5.5b (RTC Trust UX) are complete:** The firmware
  now generates a full-year JSON timetable to `/storage`, supports uploading
  a mosque's custom timetable, and features a 3-state display (`Starting...`,
  `--:--` for untrusted time, and the clock) with a 60s warning banner.

Prerequisite PRs: none. All plumbing exists.

---

## Phase 2 — Prayer engine + scheduler

Goal: the clock knows what Fajr/Dhuhr/Asr/Maghrib/Isha are, for a given
location and method, and does something at each.

- `svc_prayer_engine` (new component): calculation for lat/lon, method
  (MWL / ISNA / Egyptian / Karachi / Umm al-Qura), Asr madhab (Standard /
  Hanafi).
- `svc_scheduler` (new component): fires events at prayer times, sunrise
  and sunset, and day rollover. *(Partially superseded by `svc_timetable`,
  which now generates the full year schedule ahead of time.)*
  **Done in PR 5.6** for the prayer times: a 1 Hz epoch poll publishes
  `EVENT_PRAYER_TIME_REACHED` once per prayer per day (adhan tier only --
  `PRAYER_KIND_JAMAT` is reserved). Sunrise / sunset / day rollover stay
  with `svc_time` + `svc_prayer_engine_service`. The decision logic is
  pure C (`scheduler_logic.c`) and unit-tested on the host in
  `prayer_host_tests/test_scheduler.cpp`.
- `svc_config` (new component): NVS-backed settings -- location, method,
  madhab, iqamah offsets, adhan mode, display preferences. *(Landed as PR 12;
  the schema is in `components/svc_config/include/svc_config.h`.)*
- Wire to the display: next prayer name and countdown on the face.
- Phase 2 prayer-times refinement (seasonal jamat, presets, tomorrow
  notification) is specified in [prayer-times-model.md](prayer-times-model.md).

---

## Phase 3 — Audio

Goal: the adhan plays at prayer time.

- `svc_audio` (new component): loads a WAV / raw PCM asset from
  `/storage` and feeds `HAL_Audio_Play()` in chunks.
- No MP3 decoder exists anywhere in the tree (`audio_play_mp3` returns
  `false` by design). Either ship WAV assets or add a decoder
  (Helix / minimp3) as its own PR.
- Audio is already non-fatal: a failed `HAL_Audio_Init()` is logged and
  boot continues.

---

## Phase 4 — Configuration web UI

Goal: the user sets WiFi, location, method and TZ without reflashing.

- `svc_http` (new component): ESP-IDF `esp_http_server`.
- Serves a small settings page. V3's `index.html` (outside this repo) is
  a starting reference for fields and layout; V4 endpoints differ.
- Writes NVS: `wifi/ssid`, `wifi/pass`, `time/tz`, `ntp_server_0..2`,
  plus the future location / method keys.
- Factory reset: writes `BOOT_FLAG_FACTORY_RESET` and reboots. Landed as
  PR 4.5 (`POST /api/factory-reset` -> NVS `boot_req/flag` ->
  `boot_manager` on next boot). The BOOT-button path remains as a dev
  convenience and is scheduled for removal in a follow-up.

---

## Phase 5 — OTA

Goal: firmware updates over the network.

- **Landed (PR 4.7): local upload.** `POST /api/ota` in `svc_http`
  accepts a raw `.bin` (`application/octet-stream`), gated by the
  `X-OTA-Token` header, and streams it to the inactive OTA slot via
  `esp_ota_begin` / `esp_ota_write` / `esp_ota_end` /
  `esp_ota_set_boot_partition`. Reboots on success; `esp_ota_abort()`
  on any failure. Compile-time gate: `#error` if
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is not set.
- **Remaining: `svc_ota` component with `esp_https_ota`.** Pull-based
  OTA from an HTTPS URL, meant for fleet management. The upload path
  above is the "flash by hand from a browser" workflow that unblocks
  firmware updates today.
- App rollback prerequisite: **met** (PR 4.6). See `docs/boot.md` §2.
- Build gotcha (still applies): a change to `sdkconfig.defaults` has no
  effect until the persisted `sdkconfig.single_mcu` is deleted.

---

## Phase 6 — RTC alarm wake (follow-up)

Goal: stop waking the CPU every second just to compare a clock.

- Drive `svc_scheduler` from the DS3231 INT / SQW output (alarm 1 + alarm 2)
  instead of the 1 Hz poll, waking the SoC only for the next prayer instant.
  No correctness change is needed for the switch-over: matching is by epoch
  and `last_fired_epoch[]` only moves forward, so a late or missed wake
  fires the prayer on the next tick instead of skipping it.
- Prerequisite: `hal_rtc.h` exposes no alarm API today. Adding one is a HAL
  (and then HIL) change before `svc_scheduler` can use it.

---

## Phase 7 — Per-prayer DST offset (follow-up)

Goal: remove the known one-hour error on DST-transition days.

- `svc_scheduler` converts (local date, local seconds,
  `tz_offset_minutes`) to UTC with one offset -- the offset at **noon** of
  the target date, as published by `svc_prayer_engine_service`. On a
  spring-forward / fall-back day a prayer that falls *before* the
  transition therefore fires up to 1 h late / early. It can never
  double-fire (epoch comparison, never `(hour, minute)`).
- The fix belongs in `svc_prayer_engine_times_t`: carry a per-prayer offset,
  or the UTC instant of each prayer, so consumers do not have to
  reconstruct the transition themselves. `docs/prayer-times-model.md` is the
  domain source of truth and must be updated before the change.

---

## Phase 8 — Polish

Not scheduled; listed for completeness.

- Theme engine (colours, fonts, layout presets).
- Health / diagnostics registry surfaced as events.
- Retry / recovery for sensor probes (`STATUS.md` known issues).
- Kernel boundary cleanup: `core_os` still calls
  `display_manager_init()` and `display_manager_process_frame()`, which
  Constitution §7 records as a known violation (kernel calling into the
  layer above).
- HIL graphics adapter, so the application band stops calling `hal_gfx`
  directly (Constitution §11).
- Deferred `src/` cleanup: `src/svc/svc_scheduler.c` (a 7-line
  placeholder that no build ever referenced) was deleted in the
  scheduler PR (5.6). The rest of `src/app/*` and `src/svc/*` are empty
  placeholders that are not built (`platformio.ini` sets
  `src_dir = main`); removing the directory itself needs a separate
  change because `src/CMakeLists.txt` still globs it.
- CI hardening: bump `actions/*` to v5, pin `ubuntu-24.04`.
- ✅ Test harness v1 (mock NTP + scenario runner + reference scenario, 2026-10-31)

---

## How to use this document

- Before starting a PR, check which phase it belongs to and whether that
  phase's prerequisites are met.
- If a phase is blocked -- Phase 1 needs the panel -- pick an unblocked
  item from a later phase or from the `STATUS.md` known-issues list.
- Anything not listed here is either already implemented
  (`STATUS.md` "Implemented") or not yet decided. This document does not
  replace `STATUS.md`: `STATUS.md` remains authoritative for what is
  implemented versus planned today.
