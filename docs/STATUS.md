# Project Status

Snapshot reviewed 2026-10-04 at commit `65dd77d`. Update this file in
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
- **Kernel:** event bus, tasks, safe-mode skip.
- **Services:** `svc_network` (WiFi station), `state_manager`,
  `display_manager`, `ui_producer`, `font_engine`, `font`.

---

## Planned services (design decided, not built)

### `svc_time`

Component does not exist yet. Design decisions taken 2026-10-11:

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
  `localtime_r()` on the UTC time for display. Newlib's TZ handling
  applies the DST rule automatically; no "midnight transition handler"
  code is required. A timer at the next transition instant may be
  registered purely to trigger a UI redraw -- it does not perform any
  correction itself.

- **Zones come from a small POSIX TZ string table**, not a bundled IANA
  tzdata. The dropdown covers ~5-6 zones with their TZ strings hardcoded
  in `svc_time`. For zones outside the dropdown, the web-config side
  resolves a pasted Olson ID (`Europe/London`) to a POSIX TZ string and
  pushes that; the device never parses IANA tzdata.

- **NTP is the only sync source.** It provides UTC; `svc_time` writes
  UTC to the RTC and re-derives local on the display path.

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
| ST7796S `write_text` | not implemented in backend |
| `src/app/*`, `src/svc/*` | empty placeholders; **not built** (`platformio.ini` sets `src_dir = main`) |
| Prayer engine, scheduler, config service, theme registry, web config | do not exist |
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
3. **I2C and audio fallback pins** (21/26, 25/26/27) can collide with
   the I2S data pin and flash pins. Should fail instead of guessing.
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
10. **Audio:** `i2s_new_channel` result unchecked; no stop/abort;
    blocking writes with no timeout.
11. **Display router:** custom SDK driver unreachable when a built-in
    backend is selected; mock replaces rather than adds; lock has no
    timeout.
12. **HAL failure policy:** a display or SPI init failure aborts the
    whole boot. Consider degrading instead (the clock should keep
    time).
13. **Build hygiene:** `IDF_PATH` include hacks in the HAL CMake,
    global `-I` in `platformio.ini`, fake
    `CONFIG_AUDIO_TEST_SIMULATION` define, app rollback off, coredump
    off.

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
