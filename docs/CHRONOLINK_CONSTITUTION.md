# ChronoLink V4 OS — Project Constitution

Mandatory rules for all development, fixes, refactors and generated
code. Amended 2026-10-04 to match the code at commit `65dd77d`:
layers are aligned with [Architecture.md](Architecture.md), and real,
accepted exceptions are written down instead of hidden.

If a rule below disagrees with the code, the code wins — report the
discrepancy and fix the document, or fix the code. Do not leave the
two in disagreement.

---

## 1. Architecture layers

Bottom-up: Flash layout (0), PDL (1), HAL (2), HIL (3), Kernel (4),
Services (5), Application (6). `boot_manager` is the startup
orchestrator, not a layer.

- Lower layers never call higher layers.
- Higher layers call lower layers only through public headers.
- No circular dependencies.
- Services and the application use the HIL where an adapter exists.

The canonical mapping of layers to components is in
[Architecture.md](Architecture.md) §1.

---

## 2. Boot rules

`boot_manager` must:

- configure logging first (so later failures are visible),
- handle factory reset when the boot flags request it,
- initialise NVS, then mount FATFS on `/storage`, then log OTA state,
- call `HAL_Init()`, then `core_os_init()` and `core_os_start()`.

**Forbidden in `boot_manager`:** starting its own tasks, UI/display
access, touching services, mounting any filesystem other than FATFS.

**Accepted exceptions (documented):**

- It calls `HAL_Init()` once, as the provisioning step.
- It reads the BOOT pin (GPIO0) at startup to detect a
  factory-reset request held for 3 s. This is a development
  convenience, and it is the only hardware access permitted in
  `boot_manager`. It drives nothing and opens no bus. It is expected
  to be removed in favour of a config-driven reset produced by the
  web UI. See [boot.md](boot.md) §"Reset mechanisms" and
  [STATUS.md](STATUS.md).

---

## 3. Filesystem rules

ESP-IDF 6.0.1 under PlatformIO: **FATFS only**.

- **Allowed:** component `fatfs`, header `esp_vfs_fat.h`, partition
  type `fat`, partition name `storage`, base path `/storage`,
  wear-levelled mount.
- **Forbidden:** SPIFFS, `esp_spiffs`, LittleFS, `esp_littlefs`,
  `vfs_littlefs`.

---

## 4. CMake rules

All components declare `REQUIRES` / `PRIV_REQUIRES` using valid
ESP-IDF 6 component names, e.g. `esp_common`, `nvs_flash`, `fatfs`,
`app_update`, `esp_timer`, `esp_event`, `esp_system`, `log`, `driver`,
`esp_driver_gpio`, `esp_driver_i2c`, `esp_driver_spi`,
`esp_driver_i2s`.

Invalid names: `esp_log` (use `log`), and any SPIFFS/LittleFS name.

Do not rely on global include paths (`-I` in `platformio.ini`) to hide
a missing `REQUIRES`.

---

## 5. PlatformIO rules

`platformio.ini` must contain `framework = espidf` and
`board_build.filesystem = fatfs`, with no SPIFFS/LittleFS flags and no
references to removed components.

Do not define fake `CONFIG_*` macros in `build_flags`.

---

## 6. Partition table rules

`partitions.csv` (root) must contain
`storage,data,fat,0x820000,0x7E0000,`, must not overlap app
partitions, and must not use SPIFFS or LittleFS.

Current layout: see [partitions.csv](partitions.csv).

`docs/partitions.csv` is a copy of the root file for reader
convenience and must stay identical to it.

---

## 7. Kernel rules

`core_os` creates FreeRTOS tasks, queues and timers, manages dual-core
scheduling (core 0 system / network, core 1 UI / clock), and owns the
event bus.

**Forbidden in `core_os`:** mounting filesystems, initialising NVS,
touching hardware directly.

**Known violation (to be fixed, not a precedent):** `core_os`
currently calls `display_manager_init()` and
`display_manager_process_frame()`, which is a kernel-to-service call.
See [STATUS.md](STATUS.md).

---

## 8. HAL and PDL rules

HAL drivers contain only hardware access, expose clean APIs, and never
call services or the application. The PDL holds board facts (pins,
capabilities) and nothing else.

**Known violation (to be fixed):** `hal_gfx` (drawing primitives) is
rendering logic and belongs above the HAL.

---

## 9. HIL rules

HIL adapters contain no hardware logic and no ESP-IDF peripheral
headers. Functions that cannot be honoured (for example MP3 playback
or brightness) must return failure, never pretend to succeed.

---

## 10. Services rules

Services contain logic engines, use the HIL, never call
`boot_manager`, and never mount filesystems. They communicate through
the event bus.

---

## 11. Application rules

The application contains UI and display logic, uses services, and
never calls the HAL directly.

---

## 12. Honest-stub rule

A function that is not implemented returns an error (`HAL_ERR_DEV` or
`false`) and is listed in [STATUS.md](STATUS.md).

Returning success from a stub is **forbidden**. Existing MAX7219 and
remote display backends, and `HAL_Audio_SetVolume`, violate this rule
today and are tracked in STATUS.md.

---

## 13. Agent behaviour rules

An AI coding agent must:

- stay inside this architecture,
- never propose SPIFFS / LittleFS,
- validate CMake component names, ESP-IDF 6 and PlatformIO
  compatibility, boot order, header resolution, and the dependency
  graph,
- avoid circular dependencies,
- treat documents as secondary to the code — if they disagree, report
  it and fix the document.

---

## Goal

Keep the architecture honest and the build clean under PlatformIO with
ESP-IDF 6.0.1 and FATFS.