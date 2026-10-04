# ChronoLink V4 OS — Documentation Index

ChronoLink V4 OS is the firmware for the ChronoLink V4 clock.
Target: ESP32-S3 module, ESP-IDF 6.0.1, built with PlatformIO, FATFS only.

**If a document disagrees with the code, the code wins — the document is
a bug.** Please fix the document, and add an entry to `STATUS.md` if the
gap is worth tracking.

The commit at which these docs were last reviewed is recorded only in
`STATUS.md`. Other docs describe the code generally and do not pin
commits, so a single place stays authoritative.

---

## Design intent (short)

- **No user-facing buttons.** The BOOT and EN buttons on the PCB are
  development aids only. Users never touch them.
- **Everything is configured through a web UI** — WiFi, time, prayer
  settings, display preferences, firmware updates, factory reset.
  The web UI is *not built yet*; see `STATUS.md`.
- **Config drives behavior.** Where the code reads a "boot flag" or
  similar, the producer is expected to be the web UI in a future PR.

---

## Read in this order

| Doc | Answers |
|---|---|
| [Architecture.md](Architecture.md) | The canonical layer model, dependency rules, component map, known deviations |
| [boot.md](boot.md) | What `boot_manager` does at startup, exactly, in order |
| [hal.md](hal.md) | PDL + HAL reference: init order, APIs, status codes, pins, drivers |
| [hil.md](hil.md) | HIL adapters (`components/hil`) — the API services use |
| [partitions.md](partitions.md) | Flash layout, explained. Authoritative file is `../partitions.csv` |
| [CHRONOLINK_CONSTITUTION.md](CHRONOLINK_CONSTITUTION.md) | Mandatory rules, including documented exceptions |
| [rules.md](rules.md) | Short rule summary for AI coding agents |
| [STATUS.md](STATUS.md) | Implemented vs stubbed vs planned; known issues; open decisions; reviewed commit |

---

## Build quick start

pio run -e single_mcu # build
pio run -e single_mcu -t upload
pio device monitor # 115200 baud


`split_mcu_main` and `display_mcu` environments exist in
`platformio.ini` but have no implementation behind them. See
`STATUS.md`.

Notes: an older README mentioned `idf.py`, `assets/hw_profile.json`
and `components/ld/psram.ld`. Those files do not exist in the repo.
The build is PlatformIO-first.

---

## Retired documents

These were removed from the repo because they described designs that
were never built (or have since changed) and were a source of
confusion. Git history preserves them if needed.

| Retired file | Why removed | Replaced by |
|---|---|---|
| `ArchitectureDiagram.md` | ASCII diagram of an "OS Service Layer" with `os_*` APIs that do not exist; corrupted text encoding | `Architecture.md` |
| `layers.md` | Referenced `src/platformDependentLayer`, `src/hardwareAccessLayer` — paths that do not exist | `Architecture.md` |
| `modules.md` | Same stale path references | `Architecture.md` §1 |
| `HDL_HIL_Architecture.md` | Proposed YAML config from SPIFFS, runtime driver packs, role-name APIs (`hal_register_driver`, `hal_read_role`). None exist. SPIFFS is forbidden | `hil.md` |
| `pinout.csv` | Contained errors (UART0 on IO36/37, JTAG swapped, SD_MODE on IO1, backlight mismatch). Regenerate from schematic | `hal.md` §4 pin table |
| `partitions.csv` | Duplicate of the root file that had already drifted out of sync | `partitions.md` |