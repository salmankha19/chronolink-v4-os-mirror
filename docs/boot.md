
---

## `docs/boot.md`

```markdown
# Boot Sequence

Describes what the code does today, in order.
Target: ESP32-S3 module on the ChronoLink V4 PCB, 16 MB flash.

> The module variant is documented in `docs/hardware.md`: the bench board is
> an ESP32-S3-WROOM-1-N16R8, the production BOM is N16R2, and PSRAM is disabled
> in `sdkconfig.defaults` so one image runs on both.
> It affects which GPIOs are safe and whether PSRAM is available; the GPIO
> question is tracked in `STATUS.md`.

---

## Stages

1. **ROM bootloader → second-stage bootloader** (ESP-IDF default).
   Reads the partition table, picks an app slot using `otadata`.
   There is no `factory` partition: `app0` is `ota_0` and `app1` is
   `ota_1`. The first ever boot boots `app0`. Secure boot and flash
   encryption are currently **off**; app rollback is enabled (see §2).

2. **`app_main()`** (`main/main.c`) calls `boot_manager_init(0)`.

3. **`boot_manager_init()`** runs these steps in order:

   1. Configure logging (hoisted first so later failures are visible).
   2. Factory-reset check — see "Reset mechanisms" below.
   3. Initialise NVS (erase and retry on "no free pages / new
      version").
   4. Mount FATFS (`storage` partition) at `/storage` with wear
      levelling; format on mount failure.
   5. Log OTA running / boot / next partitions (log only, never
      fatal).
   6. `HAL_Init()` — see below. Failure aborts boot (returns -1).
   7. `core_os_init()` — creates the event bus and state queue,
      initialises the display manager.
   8. Publish stage events, call `core_os_start()`, publish `HANDOFF`
      and `READY`. `core_os_start()` creates the kernel event-bus
      dispatcher (`event_dispatch_task`) **first**, before `state_q`,
      `core0_task` and `core1_task`, so the stage events published
      around this step are drained instead of being evicted from the
      32-slot queue.

4. **Back in `app_main()`:** register the built-in font, start
   `svc_config` (loads the NVS namespace "config" into RAM; non-fatal,
   defaults on failure), start `ui_producer`, start `svc_time` (reads
   the RTC, applies the POSIX timezone and subscribes to
   `EVENT_NETWORK_CONNECTED` -- it has to be up before `svc_network`
   can publish one), load the year timetable, start
   `svc_prayer_engine_service`, start `svc_network` (non-fatal; a cold-boot
   bring-up failure arms the bring-up retry ladder instead of leaving
   the clock with no link -- see "WiFi recovery mode" below), start
   `svc_http`, then start `svc_audio` (only when `svc_http_start()`
   returned `ESP_OK`, and before the scheduler so the first prayer event
   already has a subscriber; non-fatal), then `svc_scheduler_init()` /
   `svc_scheduler_start()`, then the optional audio test, then
   `esp_ota_mark_app_valid_cancel_rollback()`, then idle. From then on
   `svc_time` ticks its snapshot once a second (RTC when present,
   otherwise the libc clock once NTP has synced), so the time it serves
   keeps advancing.

   **`svc_audio` does not touch audio during boot.** I2S is initialised
   on its own task at the first adhan and deinitialised immediately
   after, so the log line `MAX98357A I2S ready` belongs to the first
   playback, not to the boot sequence (see "HAL_Init() order", step 8).

Events can only be published after step 3.7; the event bus does not
exist earlier. Steps 3.1–3.6 are observable on the serial log only.

**Post-Boot Autonomy:**
Once step 4 completes, the system is fully autonomous. `svc_time` ticks at 1 Hz, keeping the internal clock alive. If the RTC is present, it is read every second to keep the snapshot accurate. If Wi-Fi connects, NTP corrects the RTC. Prayer times are calculated immediately on boot and recalculated at midnight (00:01) and upon any successful NTP sync.

---

## WiFi provisioning mode (recovery and first-boot setup)

Wrong saved Wi-Fi credentials used to lock the clock out of the network
permanently: `svc_network` retried forever and neither switch on the PCB is
user-accessible (SW1/SW2 are inside the case). CR #2 adds an **in-place**
provisioning AP -- no reboot -- so an installer standing next to the clock can
correct the credentials over Wi-Fi. CR #4 gave that AP the same identity as the
first-boot/unconfigured AP (`ChronoLink-<MAC4>`, WPA2-PSK, one generated
passphrase per entry), so the two cases are indistinguishable on the air and
the same on-panel screen serves both. The full design is in
[sessions/cr2-wifi-lockout-design.md](sessions/cr2-wifi-lockout-design.md); the
architecture summary is section 4.3 of
[Architecture.md](Architecture.md).

**Entry condition.** Nothing changes at boot unless the saved credentials fail.
Each `WIFI_EVENT_STA_DISCONNECTED` carrying an auth-class reason -- `202
AUTH_FAIL`, `15 4WAY_HANDSHAKE_TIMEOUT`, `205 CONNECTION_FAIL`, `204
HANDSHAKE_TIMEOUT`, `2 AUTH_EXPIRE` -- increments an auth-streak counter; any
other reason resets it to 0, so a router that is merely down (`201 NO_AP_FOUND`,
`200 BEACON_TIMEOUT`) is never mistaken for wrong credentials (#131 R1). `205`
counts because it means the AP was seen and the association or handshake was
rejected, which is the wrong-password signature on routers that do not answer
`15`/`202`/`204`. On the fifth consecutive auth-class failure a
250 ms one-shot spawns the entry worker, which tears the STA stack down and
brings the provisioning AP up. On a mis-provisioned device that is roughly 30 s
after power-up: four backoff waits (1 s + 2 s + 4 s + 8 s), five handshake
attempts, plus the 250 ms trigger.

**Slow path (PR #157).** Not every lockout is a credential rejection: a driver
that keeps failing to come up, or an AP that answers `NO_AP_FOUND` forever, used
to leave the clock retrying STA indefinitely. A second counter, incremented by
every `STA_DISCONNECTED` regardless of reason, therefore triggers the same
provisioning-AP entry at `SVC_NETWORK_UNREACHABLE_FAIL_THRESHOLD` (24) -- roughly
10 min at the capped 30 s backoff. Both counters obey the same guard, so nothing
is counted during the AP session or during the 10 min cooldown after it, and a
successful `GOT_IP` clears both.

**Unconfigured or empty SSID.** "No credentials in NVS" and "an empty SSID
stored" both take the provisioning AP at boot: an empty SSID can never
associate, so treating it as configured would only burn the retry ladder. An
empty *password* with a non-empty SSID stays valid -- that is an open network.

**Cold-boot bring-up failure.** If `svc_network_start()` fails outright at boot
(provisioning-AP driver bring-up, or the STA `esp_wifi_set_mode` / `_set_config` /
`_start` sequence) it arms the bring-up retry ladder (1 s -> 30 s, never gives
up) before returning `ESP_FAIL`. The retry worker calls `svc_network_stop()`
then `svc_network_start()`, which re-enters the correct branch; the failure path
leaves `s_started` set so that `stop()` can deinitialise a half-built driver --
without that the retry would fail on `ESP_ERR_WIFI_INIT_STATE` forever.

**The provisioning AP.** SSID `ChronoLink-<XXXX>`, where `<XXXX>` is the last
two STA MAC bytes (`%02X%02X`, 15 characters total -- the h=18 glyph-width
arithmetic in the design doc puts that inside a 320 px panel, though the
on-panel check is still open). WPA2-PSK on channel 1 with up to 4 clients, for
both entry paths -- a mis-provisioned device and an unconfigured one raise the
same AP. The passphrase is 16 uppercase hex characters from
`esp_fill_random()` (8 bytes), regenerated at every entry and never written to
NVS. `ui_producer` replaces the clock face with one provisioning screen showing
the SSID and the passphrase (plus `192.168.4.1` and "Auto-exits when idle"); the
normal web UI is served on that AP, with the shared token exempted for the
commissioning endpoints `/api/wifi`, `/api/config`, `/api/timetable` and
`/api/reboot`. A bench board with no panel has nowhere to display that screen,
so building with `-DCHRONOLINK_DEBUG_PRINT_TOKEN=1` (set in the git-ignored
`platformio.local.ini` for `env:single_mcu_debug`, the same flag `svc_http.c`
uses for the API token) makes `start_ap_mode()` also log
` AP passphrase (bench debug): <pass>` on the serial line right after the SSID,
for either entry path. The line is compiled out entirely when the flag is
undefined.

**Exit conditions (both APs).** One timer enforces two bounds: 15 min with no
HTTP activity (any exempted request restarts the idle window, on either AP) or
30 min from entry regardless of activity, whichever comes first. The bounds are
armed by `arm_ap_timeout()`, which the lockout entry worker and
`start_ap_mode()` both call, so a first boot and a lockout are timed the same
way. How the exit resolves depends on which AP was up.

On the **lockout path** the exit worker runs `svc_network_stop()` followed by
`svc_network_start()`, i.e. back to STA with the (now corrected) credentials,
and arms the cooldown. Nothing is rebooted and timekeeping never stops --
`svc_time` keeps ticking and the display returns to the clock face.

On the **setup path** there are no credentials to return to, and the
empty-SSID branch of `svc_network_start()` would raise the setup AP again on
the spot -- an exit/entry loop. The exit worker therefore does **not** call
`start()`. It holds the link down for a deterministic 10 min
(`SVC_NETWORK_COOLDOWN_US`, armed as `s_setup_reentry_timer`, with both failure
counters frozen while it runs); when that one-shot fires, a worker calls
`svc_network_stop()` + `svc_network_start()` and the setup AP comes back with a
rotated passphrase. Net effect: an unconfigured clock shows the setup screen
for at most 15 min of inactivity (30 min absolute), is off the air for 10 min,
then returns. That is a timer rather than "let STA fail until the 24-strike
slow path" because an empty SSID cannot associate: the only STA outcomes are a
config rejection or a `NO_AP_FOUND` storm, and neither is a predictable
countdown. `svc_network_start()` checks the same flag, so a stray start during
the cooldown defers to the timer instead of bouncing the AP.

**Cooldown.** For 10 min after exit, auth-class failures are not counted, so a
still-wrong password cannot bounce the device straight back into recovery. The
cooldown is cleared by `IP_EVENT_STA_GOT_IP` -- not by
`WIFI_EVENT_STA_CONNECTED`, because on WPA2-PSK association succeeds before the
4-way handshake fails, so clearing on connect would clear it on the very first
retry with still-wrong credentials -- or by its own one-shot expiring. It is
deliberately not stopped or cleared by `svc_network_stop()`: a cooldown that
outlives its own recovery cycle is intentional.

**Power-cycle escape hatch.** The auth-fail counter, the cooldown flag and the
recovery guard flags live in RAM only; nothing about recovery state is
persisted. Pulling power clears all of them, so the same wrong credentials
re-enter recovery in about 30 s (five failures on the backoff ladder plus the
250 ms trigger) and a fresh passphrase is generated. That is the documented last
resort when the 10 min cooldown is still running and the installer wants back in
immediately. It also means the device is never permanently unreachable: the
worst case is always "power-cycle and try again".

**Residual risk.** The token exemption is bounded to four endpoints
(`/api/wifi`, `/api/config`, `/api/timetable`, `/api/reboot`) and, on either AP,
to the 30-minute absolute cap. An actor who passes it already
knows the WPA2 passphrase, i.e. is physically present -- which `svc_http.c`
already declares out of scope for CSRF. But a rogue-AP override via
`POST /api/wifi` pivots through `svc_time`'s NTP to prayer times, so it is not
"just recoverable": that is why the `application/json` content-type requirement
on those writes is mandatory rather than optional. It restores the CSRF defence
the shared token was providing, in a form an HTML `<form>` cannot satisfy. GET
requests carry no such requirement, because they are side-effect free -- and in
fact no GET handler calls `require_auth()` at all, so reads (`/`, `/api/config`,
`/api/timetable`, `/api/prayers`, `/debug/ntp`) are open in every mode, not just
in AP mode.

---

## Reset mechanisms

The phrase "factory reset" is overloaded. Three distinct mechanisms
exist in ESP-IDF; **only the first is implemented in this codebase
today**.

### 1. Data reset — implemented, producer is `POST /api/factory-reset`

`boot_manager_init()` checks the boot flags for
`BOOT_FLAG_FACTORY_RESET`. If present, it erases NVS, reformats
`/storage`, and reboots.

Two producers exist:

- **Parameter path:** a caller can pass `BOOT_FLAG_FACTORY_RESET` to
  `boot_manager_init()`. This runs *before* `init_nvs()`, so it still
  works on a device whose NVS is unreadable.
- **Web UI path (PR 4.5):** `POST /api/factory-reset` writes a one-shot
  flag to NVS namespace `boot_req` (key `flag`) and reboots. On the next
  boot, `boot_manager_init()` reads and clears the flag immediately after
  `init_nvs()` and runs the same `do_factory_reset()`.

Effect:

- NVS erased (whole partition).
- `/storage` reformatted (FATFS).
- `otadata` **not** touched.
- `phy_init` **not** touched.
- Reboot.

Deliberately preserved across a data reset:

- `otadata` — boot-slot preference is not user data.
- `phy_init` — RF calibration is board-specific, not user data.

### 2. App rollback — implemented (PR 4.6)

ESP-IDF supports automatic rollback when a newly-flashed OTA image
fails to mark itself valid. This is now enabled
(`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `sdkconfig.defaults`).

The mechanism:

1. On boot after an OTA update, the new image starts in **pending
   verify** state.
2. `app_main()` calls
   `esp_ota_mark_app_valid_cancel_rollback()` after every service has
   started: `svc_config`, `svc_time`, `svc_timetable`,
   `svc_prayer_engine`, `svc_network`, `svc_http`, `svc_scheduler`.
   This transitions the image to **valid** and cancels the rollback
   timer. (#129 R2 moved this call from `boot_manager_init()`, where
   it ran before any service was up, so crashes in service init were
   not caught by rollback.) **(CR #3, 2026-11-15)** The call is gated
   on `svc_http_start()` succeeding: a firmware with no HTTP server
   has no recovery path (no config UI, no OTA endpoint, no reboot
   endpoint). If `svc_http_start()` fails, `app_main()` calls
   `esp_ota_mark_app_invalid_rollback_and_reboot()` instead and parks
   in a safe loop; the image is never marked valid. The other services
   remain non-fatal — the clock still runs, adhans are skipped, and
   offline behaviour degrades gracefully. A "stability delay" (mark
   valid after N seconds of clean run) is deliberately not added: it
   would delay every healthy boot for no failure mode it would
   actually catch.
3. If the new image crashes, hangs, or fails any earlier init step
   (`HAL_Init()`, `core_os_init()`, `core_os_start()`) and resets before
   reaching that call, the bootloader reverts to the previous slot on
   the next reset.

The call is a no-op on a factory-flash device or on any boot of an
already-validated image (`ESP_ERR_OTA_ROLLBACK_INVALID_STATE`); that
path is logged at DEBUG, not WARN.

### 3. Factory-partition boot — NOT implemented

There is no `factory` partition in `partitions.csv`. Both app slots
are OTA slots. A device whose current slot is unrecoverable cannot
boot to a known-good image without USB reflashing.

See STATUS.md "Planned boot work".

### The BOOT and EN buttons (schematic SW1 and SW2)

- **SW1 "Reset"** pulls the ESP32-S3 `EN` pin low, through a 10K
  pull-up (R12) and a 100 nF debounce capacitor (C10). Pressing it
  resets the chip.
- **SW2 "Boot"** pulls GPIO0 (net `BOOT_IO0`, module pin 27) low,
  through a 10K pull-up (R13), with no debounce capacitor. GPIO0 has
  no other connection on the PCB. Pressing it low *during a chip
  reset* puts the ESP32-S3 into ROM serial-download mode. That is a
  chip-level feature that runs before any firmware, used by
  `esptool.py` and similar tools.

The switches are enclosed inside the ChronoLink V4 case and require
disassembly to reach. They are not user-accessible in the shipped
product.

**No firmware path reads these pins.** An earlier `boot_manager.c`
read GPIO0 at boot to detect "hold for 3 s to factory reset". It was
removed because the path was effectively unreachable: holding GPIO0
through a reset enters ROM download mode (firmware never runs), and
pressing it only after reset leaves a window of tens of milliseconds
to begin a 3 s hold. A factory reset is now requested only through
`BOOT_FLAG_FACTORY_RESET`.

---

## `HAL_Init()` order

1. `pdl_board_init()` — validate PDL pins, preconfigure GPIO (I2C
   pins, SPI pins, display DC/RST/BL, optional RTC INT).
   **Failure is fatal.**
2. `HAL_GPIO_Init()`
3. `HAL_I2C_Init()` — pins come from `pdl_pins.h` only. An invalid
   SDA/SCL (`-1`, or outside `0..GPIO_NUM_MAX-1`) logs an error and
   leaves the bus down; nothing is substituted. **Not fatal:** the boot
   continues, and `HAL_RTC_Init()` / `HAL_Sensors_Init()` are skipped.
4. `HAL_SPI_Init()` — sole owner of the shared SPI bus.
   **Fatal on failure.**
5. `HAL_RTC_Init()` — soft-fails if no DS3231M answers; skipped
   entirely when the I2C bus is down.
6. `HAL_Display_Init()` — headless build returns `HAL_OK`; a
   configured backend that fails to init is **fatal**.
7. Register sensor drivers present on this board (per
   `pdl_capabilities.h`), then `HAL_Sensors_Init()` (always returns
   `HAL_OK`; per-sensor failures are only logged).
8. **Audio is NOT initialised here** (changed with `svc_audio` v1).
   `HAL_Init()` used to call `HAL_Audio_Init()`, which left an enabled
   I2S channel idle for the entire uptime. I2S is now brought up by
   `svc_audio`'s task at the first playback and torn down as soon as the
   playback ends, so there is no idle I2S instance and no
   Play-vs-Deinit race between tasks. Consequences:
   - `MAX98357A I2S ready` / `MAX98357A I2S deinitialized` appear around
     an adhan, not at boot.
   - A board with a dead amplifier fails at playback time
     (`EVENT_AUDIO_ERROR`, stage=HAL_INIT) instead of logging a warning
     during boot. Nothing in the boot path depends on audio.
   - `CONFIG_AUDIO_TEST_ENABLE` (off by default) still drives the HAL
     directly; it is a bench harness, not a boot step.

---

## Flash layout

See [partitions.csv](partitions.csv). Summary:

| Name | Type | Offset | Size |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 32 KB |
| otadata | data/ota | 0x11000 | 8 KB |
| phy_init | data/phy | 0x13000 | 4 KB |
| app0 | app/ota_0 | 0x20000 | 4 MB |
| app1 | app/ota_1 | 0x420000 | 4 MB |
| storage | data/fat | 0x820000 | 0x7E0000 (≈7.9 MB) |

Total ends exactly at 16 MB. The `storage` line must stay as above
(Constitution §6).

---

## Not implemented yet

- Boot manifest (hardware-profile hash, signature checks) and
  `hw_profile.json`.
- Safe-mode trigger and policy beyond "skip service tasks".
- Boot-failure events before the event bus exists.
- OTA update service (only partition logging exists — see `STATUS.md`).
- `factory` partition — **not planned**. See `partitions.md` for the
  decision and `STATUS.md` for the recovery model.
