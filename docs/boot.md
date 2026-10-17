
---

## `docs/boot.md`

```markdown
# Boot Sequence

Describes what the code does today, in order.
Target: ESP32-S3 module on the ChronoLink V4 PCB, 16 MB flash.

> The module variant is documented elsewhere as both N16R2 and N16R8.
> This is an open question (see STATUS.md) — it affects which GPIOs are
> safe and whether PSRAM is available. PSRAM is currently **disabled**
> in `sdkconfig`.

---

## Stages

1. **ROM bootloader → second-stage bootloader** (ESP-IDF default).
   Reads the partition table, picks an app slot using `otadata`.
   There is no `factory` partition: `app0` is `ota_0` and `app1` is
   `ota_1`. The first ever boot boots `app0`. Secure boot, flash
   encryption and app rollback are currently **off**.

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
   can publish one), start `svc_network` (non-fatal), optional audio
   test, then idle. From then on `svc_time` ticks its snapshot once a
   second (RTC when present, otherwise the libc clock once NTP has
   synced), so the time it serves keeps advancing.

Events can only be published after step 3.7; the event bus does not
exist earlier. Steps 3.1–3.6 are observable on the serial log only.

---

## Reset mechanisms

The phrase "factory reset" is overloaded. Three distinct mechanisms
exist in ESP-IDF; **only the first is implemented in this codebase
today**.

### 1. Data reset — implemented, but no producer yet

`boot_manager_init()` checks the boot flags for
`BOOT_FLAG_FACTORY_RESET`. If present, it erases NVS, reformats
`/storage`, and reboots.

**There is currently no producer of that flag.** No button, no NVS
key, no config file sets it today. The mechanism exists in the code
but cannot be triggered by a user yet.

Planned producer: the configuration web UI (`svc_http`, not built) is
expected to write the flag to NVS and trigger a reboot. See STATUS.md
"Planned web / config work".

Effect:

- NVS erased (whole partition).
- `/storage` reformatted (FATFS).
- `otadata` **not** touched.
- `phy_init` **not** touched.
- Reboot.

Deliberately preserved across a data reset:

- `otadata` — boot-slot preference is not user data.
- `phy_init` — RF calibration is board-specific, not user data.

### 2. App rollback — NOT implemented

ESP-IDF supports automatic rollback when a newly-flashed OTA image
fails to mark itself valid. This is currently disabled
(`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is not set). A bad OTA
image that prevents `boot_manager` from running cannot be recovered
without USB reflashing.

See STATUS.md "Planned boot work".

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
3. `HAL_I2C_Init()` — skipped with a warning if the pins are invalid.
4. `HAL_SPI_Init()` — sole owner of the shared SPI bus.
   **Fatal on failure.**
5. `HAL_RTC_Init()` — soft-fails if no DS3231M answers.
6. `HAL_Display_Init()` — headless build returns `HAL_OK`; a
   configured backend that fails to init is **fatal**.
7. Register sensor drivers present on this board (per
   `pdl_capabilities.h`), then `HAL_Sensors_Init()` (always returns
   `HAL_OK`; per-sensor failures are only logged).
8. `HAL_Audio_Init()` — failure is logged and ignored.

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
- **App rollback** (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) — currently
  off. Must be enabled in the first firmware that ships with the
  two-slot partition table, because the bootloader cannot be updated by
  OTA. See `STATUS.md` "Planned boot work".
- `factory` partition — **not planned**. See `partitions.md` for the
  decision and `STATUS.md` for the recovery model.
- Producer of `BOOT_FLAG_FACTORY_RESET` (needs the web UI).
