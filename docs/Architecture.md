# ChronoLink V4 OS - Architecture

This is the single canonical architecture document. It supersedes the
earlier "4-layer", "6-layer" and "HDL/HIL" descriptions, which
disagreed with each other and with the code.

---

## 1. Layer model

Layers are numbered bottom-up. A layer may only call layers **below**
it, through their public headers.

| # | Layer | Components (this repo) | Role |
|---|---|---|---|
| 0 | Flash layout | `partitions.csv` (root) | Partition table, OTA slots, `/storage` FATFS |
| 1 | PDL - Platform/Board Definition | `pdl_*.h`, `board_pins.h`, `pdl_board_esp32.c` (inside `chronolink_hal`) | Pin map, board identity, capability flags, pin validation, initial pin configuration |
| 2 | HAL - Hardware Abstraction | `chronolink_hal`, `chronolink_driver_sdk` | Drivers: GPIO, I2C, SPI, RTC (DS3231M), display (router + ST7796S / MAX7219 / remote / mock / custom backends), sensors (SHT4x, BME280, VEML7700), audio (MAX98357A) |
| 3 | HIL - Hardware Independent Layer | `hil` | Thin `bool`-returning adapters over the HAL, for services (`display_api`, `rtc_api`, `sensor_api`, `audio_api`, `gpio_api`, `i2c_api`, `spi_api`) |
| 4 | Kernel | `core_os`, `event_bus` | FreeRTOS tasks, queues, event bus, safe-mode handling |
| 5 | Services | `state_manager`, `svc_network`, `svc_time`, `display_manager`, `ui_producer`, `font_engine`, `font`, `remote_bridge` | Logic and engines |
| 6 | Application | `main/` (UI code lives here today), future `src/app/*` | UI, display cycle, web config, theme |

`boot_manager` is **not a layer**. It is the startup orchestrator that
runs once, before the kernel's tasks exist. See [boot.md](boot.md).

PDL is not a separate component today: its headers and one `.c` file
live in `chronolink_hal` and are logically the bottom of that
component.

`src/app/*` and `src/svc/*` contain placeholder stubs. They are **not
compiled**: `platformio.ini` sets `src_dir = main`. Treat them as
reserved for future work, not as active code.

---

## 2. Rules (short form)

1. Lower layers never call higher layers. No circular dependencies.
2. Services and the application use the HIL where an adapter exists.
3. Only the HAL touches hardware registers or ESP-IDF peripheral
   drivers (`driver/i2c.h`, `driver/spi_master.h`, I2S, GPIO).
4. Filesystem is FATFS on `/storage` only. No SPIFFS, no LittleFS.
5. The full rule set, including documented exceptions, is in
   [CHRONOLINK_CONSTITUTION.md](CHRONOLINK_CONSTITUTION.md).

---

## 3. Dependency diagram (as built)

```mermaid
flowchart TB
    subgraph SVC["Services (5)"]
        DM[display_manager]
        UIP[ui_producer]
        NET[svc_network]
        TIME[svc_time]
        SM[state_manager]
        FE[font_engine / font]
    end
    subgraph KERN["Kernel (4)"]
        CORE[core_os]
        EB[event_bus]
    end
    subgraph HIL["HIL (3)"]
        HILC[hil adapters]
    end
    subgraph HAL["HAL (2) + PDL (1)"]
        HALC[chronolink_hal]
        SDK[chronolink_driver_sdk]
        PDL[pdl_* / board_pins]
    end
    BOOT[boot_manager<br/>startup orchestrator]

    BOOT --> HALC
    BOOT --> CORE
    BOOT --> EB
    CORE --> EB
    CORE --> DM
    DM --> HALC
    TIME --> HILC
    TIME --> EB
    HILC --> HALC
    HILC --> FE
    HALC --> SDK
    HALC --> PDL

---

## 4. Runtime model

- `app_main()` (`main/main.c`) calls `boot_manager_init(0)`, registers
  the built-in 8x8 font, starts `ui_producer`, starts `svc_time` (RTC +
  NTP; it must be up before anything can publish
  `EVENT_NETWORK_CONNECTED`), then starts `svc_network` (non-fatal if
  Wi-Fi fails; the clock keeps running from the RTC).
- `core_os_init()` creates the event bus and calls
  `display_manager_init()`.
- `core_os_start()` starts with a kernel-owned `event_dispatch_task`
  that drains the event bus and logs each received event at DEBUG level;
  it is created before `state_q` / `core0_task` / `core1_task` so the
  boot-stage events published around this call are not evicted from the
  32-slot queue. Subscriber fan-out (queue-forward) exists as of PR 5b-1:
  `event_bus_dispatch()` forwards matching events to registered
  subscriber queues. `svc_time` registers the first subscriber
  (`EVENT_NETWORK_CONNECTED`, PR 5b-2).
- `svc_time_start()` subscribes to `EVENT_NETWORK_CONNECTED` and runs NTP
  on its own worker task; a failed sync is retried by an `esp_timer`
  backoff (1 min -> 5 min -> 15 min -> 1 h). Only UTC is ever written to
  the RTC; local time is derived at read time from the POSIX TZ rule.
- `core_os_start()` then starts a state-queue task, a `core0_task`
  (system / network) and a `core1_task` (UI / clock). `core1_task`
  calls `display_manager_process_frame()` about every 16 ms.
- `ui_producer` runs one task on core 0 (`ui_prod`, `tskIDLE_PRIORITY + 2`,
  3072 B stack) that wakes every 250 ms, reads `svc_time_get_snapshot()`,
  and submits a clock frame through `display_manager_submit_frame()` only
  when the formatted local `HH:MM:SS` changes. The first frame adds a
  full-screen clear; later frames erase just the 64x8 text bounding box.
  Rendering on change keeps the 3-slot frame queue from filling.
- In safe mode (`BOOT_FLAG_SAFE_MODE`) the kernel skips the normal
  service tasks.

---

## 5. Known deviations from the rules

These exist in the code today. They are documented here so nobody
mistakes them for intended design. Each has an entry in
[STATUS.md](STATUS.md).

**Type** column: `Dev` = genuine deviation to be fixed. `Exc` =
accepted exception, written into the Constitution and expected to
stay.

| Type | Deviation | Where | Intended fix / rationale |
|---|---|---|---|
| Dev | Kernel calls a service (`display_manager`) | `core_os.c` | Have `display_manager` register itself with the kernel, or start it from `app_main` |
| Dev | A service calls the HAL directly, bypassing HIL | `display_manager`, `json_display_executor` call `HAL_GFX_*` / `HAL_Display_*` | Route through a HIL graphics adapter, or formally allow services to use the display HAL |
| Dev | `hal_gfx` (drawing primitives) lives in the HAL | `chronolink_hal/src/hal_gfx.c` | Move to its own graphics component above the HAL |
| Exc | `boot_manager` calls `HAL_Init()` | `boot_manager.c` | Accepted exception; provisioning step only. The BOOT-pin read that used to be here was removed in `refactor/remove-boot-button` |
| Dev | HAL reads pin settings from `main/Kconfig.projbuild` | `board_pins.h` uses `CONFIG_BOARD_*` | Move the pin Kconfig into `chronolink_hal` |
| Dev | `chronolink_driver_sdk` includes HAL headers privately | its `CMakeLists.txt` | Extract shared types into a small common component |

---

## 6. How to add things

- **A new display panel** - implement a `display_backend_ops_t`, export
  `HAL_DISPLAY_<NAME>_OPS`, add a Kconfig choice and one line in
  `hal_display_select_ops()`. Details in [hal.md](hal.md) section 6.
- **A new sensor** - implement a `hal_sensor_driver_t`, add a
  capability flag in `pdl_capabilities.h`, register it in `hal_init.c`
  under that flag.
- **A new service** - new component; depend on `hil` (not
  `chronolink_hal`); talk to other services through the event bus.
- **A new HIL adapter** - a thin `bool`-returning wrapper over the HAL.
  Add hardware logic to the HAL first, then wrap it here.