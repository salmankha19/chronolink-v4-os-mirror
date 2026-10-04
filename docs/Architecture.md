
---

## `docs/Architecture.md`

```markdown
# ChronoLink V4 OS — Architecture

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
| 1 | PDL — Platform/Board Definition | `pdl_*.h`, `board_pins.h`, `pdl_board_esp32.c` (inside `chronolink_hal`) | Pin map, board identity, capability flags, pin validation, initial pin configuration |
| 2 | HAL — Hardware Abstraction | `chronolink_hal`, `chronolink_driver_sdk` | Drivers: GPIO, I2C, SPI, RTC (DS3231M), display (router + ST7796S / MAX7219 / remote / mock / custom backends), sensors (SHT4x, BME280, VEML7700), audio (MAX98357A) |
| 3 | HIL — Hardware Independent Layer | `hil` | Thin `bool`-returning adapters over the HAL, for services (`display_api`, `rtc_api`, `sensor_api`, `audio_api`, `gpio_api`, `i2c_api`, `spi_api`) |
| 4 | Kernel | `core_os`, `event_bus` | FreeRTOS tasks, queues, event bus, safe-mode handling |
| 5 | Services | `state_manager`, `svc_network`, `display_manager`, `ui_producer`, `font_engine`, `font`, `remote_bridge` | Logic and engines |
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
    HILC --> HALC
    HILC --> FE
    HALC --> SDK
    HALC --> PDL