# ChronoLink V4 OS — Documentation Index

ChronoLink V4 OS is the firmware for the ChronoLink V4 clock.
Target: ESP32-S3 module, ESP-IDF 6.0.1, built with PlatformIO, FATFS only.

**Last reviewed against code: commit 65dd77d (2026-10-04).**
If a document disagrees with the code, the code wins — the document is a
bug. Please fix the document, and add an entry to `STATUS.md` if the
gap is worth tracking.

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
| [partitions.csv](partitions.csv) | Flash layout (identical to root `partitions.csv`) |
| [CHRONOLINK_CONSTITUTION.md](CHRONOLINK_CONSTITUTION.md) | Mandatory rules, including documented exceptions |
| [rules.md](rules.md) | Short rule summary for AI coding agents |
| [STATUS.md](STATUS.md) | Implemented vs stubbed vs planned; known issues; open decisions |

---

## Build quick start

```bash
pio run -e single_mcu          # build
pio run -e single_mcu -t upload
pio device monitor             # 115200 baud