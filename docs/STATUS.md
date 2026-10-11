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
  DS3231M RTC with oscillator-stop detection, display router, ST7796S
  backend, `hal_gfx`, sensor registry with SHT4x/BME280/VEML7700
  drivers, MAX98357A audio, driver SDK for one custom display driver.
- **HIL adapters** for display, RTC, sensors, audio, GPIO, I2C, SPI.
- **Kernel:** event bus, tasks, safe-mode skip.
- **Services:** `svc_network` (WiFi station), `state_manager`,
  `display_manager`, `ui_producer`, `font_engine`, `font`.

---

## Stubbed or placeholder (must not be relied on)

| Item | State |
|---|---|
| MAX7219 display backend | returns OK, draws nothing |
| Remote display backend | returns OK, sends nothing |
| `HAL_Audio_SetVolume` | returns OK, does nothing |
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
8. **RTC** writes weekday 0 (DS3231 expects 1–7) and does not
   range-check input.
9. **Sensors:** failed probe is permanent; `HAL_Sensors_Init` always
   returns OK; SHT4x CRC not checked.
10. **I2C** uses the legacy driver with deprecation checks
    suppressed, and lazy init is not under the lock. No bus recovery.
11. **Audio:** `i2s_new_channel` result unchecked; no stop/abort;
    blocking writes with no timeout.
12. **Display router:** custom SDK driver unreachable when a built-in
    backend is selected; mock replaces rather than adds; lock has no
    timeout.
13. **HAL failure policy:** a display or SPI init failure aborts the
    whole boot. Consider degrading instead (the clock should keep
    time).
14. **Build hygiene:** `IDF_PATH` include hacks in the HAL CMake,
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

- **App rollback** (ESP-IDF `BOOTLOADER_APP_ROLLBACK_ENABLE`): off.
  A bad OTA that prevents `boot_manager` from running cannot recover
  without USB reflash.
- **Factory partition:** no `factory` slot exists. Cannot revert to
  as-shipped firmware from the device.
- **Full factory reset:** current reset wipes NVS + `/storage` only.
  Does not touch `otadata`, does not force a firmware revert, does
  not clear `phy_init`. Tracked as three separate future PRs.
- **Remove BOOT-button reset code** from `boot_manager.c`: a
  development convenience that does not belong in the shipped
  product. Replace with a config-driven trigger (see "Planned web /
  config work").

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