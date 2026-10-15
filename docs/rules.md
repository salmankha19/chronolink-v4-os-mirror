# Rules for AI Coding Agents — ChronoLink V4 OS

Authoritative sources, in order:

1. **the code**,
2. `CHRONOLINK_CONSTITUTION.md`,
3. `Architecture.md`, `boot.md`, `hal.md`, `hil.md`,
4. `partitions.csv` (root),
5. `STATUS.md`.

If a document disagrees with the code, say so and update the
document. Do not leave the disagreement in place.

---

## Always

- Respect the layer order: Flash (0), PDL (1), HAL (2), HIL (3),
  Kernel (4), Services (5), Application (6). Call downward only.
- New services prefix their component name with `svc_`. The
  current component-to-layer mapping is in `components/README.md`.
- FATFS on `/storage` only. Never SPIFFS or LittleFS.
- Use valid ESP-IDF 6 component names (`log`, not `esp_log`).
- Services and the application use `hil`; only the HAL touches
  hardware.
- Check every `hal_status_t` / `bool`. Stubs return errors, never
  success (Constitution §12).
- Keep boot order: logging, factory-reset check, NVS, FATFS, OTA log,
  `HAL_Init`, `core_os_init`, `core_os_start`.
- Pins come from `pdl_pins.h`. Do not invent pins or add a second
  Kconfig entry for a pin that already has one.
- Config-driven behaviour: the intended user-facing control path is
  the web UI (not built). Buttons on the PCB are development-only.

## Never

- Add hardware access to services, HIL, or the application.
- Add `boot_manager` features beyond provisioning and hand-off.
- Reference APIs that do not exist (`HAL_RegisterDevice`,
  `hal_read_role`, `os_get_temperature`, `HAL_HealthCheck`, and
  similar). Check `hal.md` and `hil.md` for what is real.
- Introduce circular dependencies or rely on global `-I` flags.

## Before finishing a change

Validate CMake `REQUIRES`, header resolution, dependency direction,
and update the relevant doc and `STATUS.md` in the same change.