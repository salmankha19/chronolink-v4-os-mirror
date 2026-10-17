# Project Status

Snapshot reviewed 2026-10-15 at commit `6c7637a` (PR 8 in flight). Update this file in
the same change that alters any item below.

**This is the authoritative list of what is implemented vs. what is
not.** Where another doc mentions a gap, this file has the detail.

---

## Design intent

- No user-facing buttons. BOOT/EN on the PCB are development aids.
- All user configuration — WiFi, time, prayer settings, display,
  firmware updates, factory reset — goes through a web UI.
  The web UI is **not built**; see "Planned web / config work".

---

## Implemented (working code)

- **Boot:** logging, factory reset (via flag, or BOOT button held 3 s
  — dev convenience), NVS, FATFS `/storage`, OTA partition logging,
  hand-off to kernel.
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
- **Services:** `svc_network` (WiFi station), `state_manager`, `svc_config` (NVS-backed installation config: location, method, madhab, iqamah offsets, adhan mode, display preferences, high-latitude rule, adhan offsets, zawal buffer — storage + accessors only, no calculation or hardware).
- **`svc_prayer_engine` (PR 13):** pure C++ prayer time calculation. Standalone library in `components/svc_prayer_engine/include/prayertimes/` (no ESP-IDF dependency; usable outside this project). Computes Fajr, Sunrise, Dhuhr, Zawal Start, Asr, Asr Yellowing (preferred end), Maghrib, Sunset (true astronomical), Isha, Islamic Midnight for a given date, coordinates, timezone offset, and method (MWL, ISNA, Egyptian, Karachi, Umm al-Qura, Jafaria). High-latitude fallback supports AUTO (seventh-of-night above 48° latitude, middle-of-night at or below 48°), plus explicit NONE, MIDDLE_NIGHT, SEVENTH_OF_THE_NIGHT, and ANGLE_BASED rules selectable via the wrapper. Default is AUTO. **Verified against AlAdhan API** across four global cities (Horsham, Karachi, Makkah, New York) — all times within ±1 minute. Wrapper API: `svc_prayer_engine_calculate()`. The high-latitude fallback uses clamping (PrayTimes.org style) rather than NaN-only. AUTO defaults to one-seventh-of-night for latitudes > 48° (UK, Northern Europe, Canada) where twilight is unreliable, and middle-of-night for latitudes <= 48° (Middle East, South Asia). Users can override this in svc_config. Islamic midnight uses the clamped Fajr (or sunrise) to avoid falling before Isha, and Isha/midnight are allowed to exceed 86400 seconds for post-midnight times.
- **`svc_prayer_engine_service` (PR 15):** runtime service that reads `svc_config` (location, method, madhab, high-latitude rule, adhan offsets, zawal buffer) and `svc_time` (local date), calls `svc_prayer_engine_calculate()`, applies per-prayer adhan offsets (`adhan_offset_min[]`), computes zawal start (`dhuhr - zawal_buffer_min`), and publishes `EVENT_PRAYER_TIMES_UPDATED`. Public API: `svc_prayer_engine_service_start()`, `_recalculate()`, `_get_times()`. The event is a signal only (payload truncated to 32 bytes); consumers must call `_get_times()` for the full snapshot. Recalculation is triggered by `EVENT_TIME_UPDATE`, which `svc_time` currently publishes only on successful NTP sync — midnight/date-rollover recalculation is deferred to a future PR that adds date-change publishing to `svc_time`.
- **Application (layer 6):** `display_manager`, `ui_producer`,
  `font_engine`, `font` -- UI and display logic, not services
  (Constitution section 11).
- **`svc_time`:** RTC + NTP + POSIX TZ; the first event-bus subscriber
  (`EVENT_NETWORK_CONNECTED`); publishes `EVENT_TIME_UPDATE`; ticks the
  snapshot once a second (PR 5c). Details in the `svc_time` section below.
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
7. **`HAL_GPIO_Init`** still configures GPIO2 as an output (leftover
   example). Mask-versus-index handling in `hal_gpio_safe.c` is
   ambiguous, and `GPIO_PIN_COUNT` 48 rejects GPIO48.
8. **Sensors:** failed probe is permanent; `HAL_Sensors_Init` always
   returns OK; SHT4x CRC not checked.
9. **I2C** uses the legacy driver with deprecation checks
    suppressed, and lazy init is not under the lock. No bus recovery.
10. **Audio:** stop/abort is cooperative only -- `HAL_Audio_Stop()` sets a
    flag that `HAL_Audio_Play()` polls between 1024-sample chunks (~23 ms
    at 44.1 kHz) -- and nothing calls it yet. `Play()` has no lock against
    `Stop()`/`Deinit()` on another task.
11. **Display router:** custom SDK driver unreachable when a built-in
    backend is selected; mock replaces rather than adds; lock has no
    timeout.
12. **HAL failure policy:** a display or SPI init failure aborts the
    whole boot. Consider degrading instead (the clock should keep
    time).
13. **Build hygiene:** `IDF_PATH` include hacks in the HAL CMake,
    global `-I` in `platformio.ini`, app rollback off, coredump
    off.
14. **main/Kconfig.projbuild `CONFIG_BOARD_AUDIO_*` entries are dead.**
    BOARD_AUDIO_I2S_BCLK, BOARD_AUDIO_I2S_WS, BOARD_AUDIO_I2S_DOUT,
    and BOARD_AUDIO_SD_MODE are not read by any driver. The HAL audio
    driver reads pins from pdl_pins.h (PDL_PIN_I2S_*), not from these
    Kconfig entries. Leave the entries in place for now (removing them
    changes the menuconfig menu tree and would need a migration note);
    either delete them or wire them into the HAL in a future PR.
15. **NTP needs `CONFIG_LWIP_SNTP_MAX_SERVERS=3`.** `svc_time` configures
    three servers (`time`/`ntp_server_0..2`); LwIP's SNTP keeps only
    `CONFIG_LWIP_SNTP_MAX_SERVERS` of them (default `1`) and drops the
    rest. Added to `sdkconfig.defaults` by PR 5b-2, but a stale
    `sdkconfig.single_mcu` silently keeps `1`; `svc_time` logs the clamp.
16. **The `svc_time` tick relies on picolibc's `time()` / `localtime_r`**
    (via the HRT systimer). DS3231M drift is not corrected during a boot
    unless NTP resyncs; the RTC is re-read every tick when present. A
    resync that IDF runs on its own 1 h cycle
    (`CONFIG_LWIP_SNTP_UPDATE_DELAY`) steps the system clock without
    updating the RTC, so the snapshot follows the RTC and does not see it;
    subscribe to `NETIF_SNTP_TIME_SYNC` or prefer `time()` if that matters.
17. **HIL function names are unprefixed** (`rtc_*`, `i2c_*`, `spi_*`,
    `gpio_*`, `display_*`, `audio_*`, `sensor_*`) and can collide with
    IDF internal symbols when first called. `rtc_init` collided with
    `esp_hw_support`'s `rtc_init` in PR 5b-2 and was renamed to
    `hil_rtc_init`. Rename the others with a `hil_` prefix when they are
    first called.
18. **`DM_MAX_TEXT_LEN` was 8, which left no room for a NUL** on an 8-char
    string -- `"HH:MM:SS"` is exactly 8 -- so `font_engine_draw_text()`
    walked past the end of `arg.text.text` into the union and the next
    command. Latent, not theoretical: no caller of
    `display_manager_submit_frame()` existed until PR 6. Fixed in PR 6:
    the macro is 16 with guaranteed termination (`dm_cmd_draw_text()` caps
    the copy at `DM_MAX_TEXT_LEN - 1` and always writes the NUL).
19. **`display_manager/README.md` was stale** -- it claimed a lock-free
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
| OTA wrote a bad image | App rollback reverts to the previous slot (see below) |
| Both slots unbootable (e.g. power loss mid-OTA) | USB reflash — accepted limitation; USB is exposed |
| User wants to start over | Data reset: `BOOT_FLAG_FACTORY_RESET` wipes NVS + `/storage`, reboots |

**Terminology — four distinct mechanisms, four different names.** Do not
use "factory reset" as a catch-all; it means different things to different
readers.

| Name | What it does | Status |
|---|---|---|
| `BOOT_FLAG_FACTORY_RESET` → `do_factory_reset()` | Wipe NVS + reformat `/storage`, reboot. **Not an image reset.** | Exists in `boot_manager.c`; no producer yet (needs `svc_http`) |
| `factory` partition | Known-good golden image slot | Not present, **not planned** |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | Auto-revert a bad OTA to the previous slot | Currently **off**; must be enabled in the first firmware that ships with the two-slot table |
| USB reflash | Manual recovery via `esptool.py` | Always available |

### App rollback: must ship in the first firmware (not started)

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is currently `not set`. This must
flip before the first firmware ships, because:

- The bootloader is written at flash time and is **never updated by OTA**.
- Enabling rollback later requires a USB reflash of every deployed device.
- Without rollback, an OTA image that fails to boot leaves the device in a
  reset loop; the only recovery is USB reflash.

The code change is small:

1. Set `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `sdkconfig.defaults`.
2. Call `esp_ota_mark_app_valid_cancel_rollback()` in `boot_manager` once
   the new image has proven itself — after `core_os_start()` returns and
   the event bus is alive. Without this call, the new image never gets
   marked valid and the next reset rolls back.

The `factory` partition conversation reopens only if the USB-reflash
limitation becomes unacceptable for a specific deployment. A 4 MB
`factory` slot would require shrinking `app0` / `app1` to ~3.5 MB each
(or reducing `/storage`), which changes the max OTA image size.

### Other planned boot work

- **Remove BOOT-button reset code** from `boot_manager.c` — a
  development convenience that does not belong in the shipped product.
  Replace with a config-driven trigger (see "Planned web / config work").
- **Full factory reset:** current data reset wipes NVS + `/storage` only.
  Does not touch `otadata`, does not force a firmware revert, does not
  clear `phy_init`. Tracked as three separate future PRs.
- **Boot manifest** (hardware-profile hash, signature checks) and
  `hw_profile.json`. Not implemented.
- **Safe-mode trigger and policy** beyond "skip service tasks".
- **Boot-failure events** before the event bus exists.

---

## Planned web / config work (not started)

The design intent is that all user configuration goes through a web
UI. None of it exists yet.

- **`svc_http`**: HTTP server (component not created).
- **Config UI**: set WiFi credentials, time, prayer settings, display
  preferences.
- **Firmware update over HTTP**: writes to an inactive OTA slot.
- **Factory-reset trigger**: writes `BOOT_FLAG_FACTORY_RESET` to NVS
  and reboots.
- **Producer of `BOOT_FLAG_FACTORY_RESET`**: currently no producer
  exists. The code path in `boot_manager.c` is reachable only via the
  BOOT button (dev-only).

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
