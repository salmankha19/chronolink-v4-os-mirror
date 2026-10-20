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

## Current state (2026-10-14)

| Area | State |
|---|---|
| Boot | Firmware boots on the ESP32-S3 devkit; every soft-fail behaves as documented (audio, RTC absent, sensor probes). |
| Network | WiFi station connects and gets an IP; on the guest network NTP is reachable and a sync completes. |
| Event bus | `event_dispatch_task` drains the kernel queue; `event_bus_subscribe()` fans out per type (8 subscriber slots). |
| `svc_time` | First event-bus subscriber. NTP syncs, the DS3231M write path soft-fails on a bare devkit, POSIX TZ (default UK `GMT0BST,M3.5.0/1,M10.5.0`), 1 Hz tick on the snapshot, publishes `EVENT_TIME_UPDATE`. |
| `ui_producer` | 250 ms poll task on core 0; builds a 2-command `dm_frame_t` (draw_text_scaled for time, draw_text_scaled for AM/PM) only when the rendered local `H:MM AM/PM` changes, then submits it. First frame adds a one-time clear. |
| `display_manager` | Frame queue plus frame counter (`display_manager_frames_dispatched()`). Frames dispatch at 1 Hz on the devkit and the counter advances -- verified on hardware against the PR 6 boot log. |
| Panel | None wired. The ST7796S HAL initialises, RDDID returns all-zero (non-fatal by design), frames write into the void: no crash, no log spam. |
| CI | Two jobs (`PlatformIO build (single_mcu)`, then `ESP-IDF build (esp32s3)`). Artifacts upload only on tag pushes and manual dispatch (`retention-days: 7`), so ordinary push/PR runs leave no artifact storage behind. |

Components that exist today: `boot_manager`, `core_os`, `event_bus`,
`chronolink_hal`, `hil`, `chronolink_driver_sdk`, `state_manager`,
`svc_network`, `svc_time`, `remote_bridge`, `display_manager`,
`ui_producer`, `font_engine`, `font`, `audio_test`. Everything described
from Phase 2 onward does not exist yet.

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

Prerequisite PRs: none. All plumbing exists.

---

## Phase 2 — Prayer engine + scheduler

Goal: the clock knows what Fajr/Dhuhr/Asr/Maghrib/Isha are, for a given
location and method, and does something at each.

- `svc_prayer_engine` (new component): calculation for lat/lon, method
  (MWL / ISNA / Egyptian / Karachi / Umm al-Qura), Asr madhab (Standard /
  Hanafi).
- `svc_scheduler` (new component): fires events at prayer times, sunrise
  and sunset, and day rollover.
- `svc_config` (new component): NVS-backed settings -- location, method,
  madhab, iqamah offsets, adhan mode, display preferences. *(Landed as PR 12;
  the schema is in `components/svc_config/include/svc_config.h`.)*
- Wire to the display: next prayer name and countdown on the face.

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

- `svc_ota` (new component): `esp_https_ota`, or a local upload.
- Enable `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` in
  `sdkconfig.defaults` and call
  `esp_ota_mark_app_valid_cancel_rollback()` once a new image has proven
  itself. Without that call the image never validates, and a bad OTA is
  unrecoverable without USB reflashing.
- Background: `STATUS.md` "App rollback: must ship in the first
  firmware"; `docs/boot.md` "Not implemented yet".
- Build gotcha: a change to `sdkconfig.defaults` has no effect until the
  persisted `sdkconfig.single_mcu` is deleted.

---

## Phase 6 — Polish

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
- CI hardening: bump `actions/*` to v5, pin `ubuntu-24.04`.

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
