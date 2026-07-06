# Boot Sequence and Bootloader Manifest

## Purpose

This document describes the full boot sequence for ChronoLink V4 OS on the ESP32‑S3‑WROOM‑1‑N16R2 module, the role of the second stage bootloader, OTA selection, and the boot manifest format used to validate and configure boot behavior.

---

## Boot sequence summary

1. **ROM Bootloader**
   - Mask ROM executes reset vector and loads the second stage bootloader.

2. **Second Stage Bootloader**
   - Read `partitions.csv` partition table.
   - Select OTA slot (factory / ota_0) using `otadata`.
   - Configure flash and PSRAM timing.
   - Optionally verify image signature (future secure boot).
   - Load app image and pass boot flags to the application.

3. **OS Kernel Startup**
   - Initialise CPU clock and caches.
   - Start FreeRTOS scheduler.
   - Initialise logging and minimal subsystems.

4. **HAL Initialisation**
   - Memory and PSRAM initialisation.
   - Bus initialisation (I2C, SPI, UART, GPIO).
   - Mount NVS and filesystem.
   - Load hardware profile and register drivers.
   - Run health checks and post events to State Manager.

5. **Service Layer Startup**
   - Start State Manager, Event Bus, Time Engine, Sensor Engine, Prayer Engine, Network Service, Display Compositor.

6. **Application Layer Startup**
   - UI Manager, Menu Controller, Notification Manager.

---

## Second stage bootloader responsibilities

- **Partition table parsing**: ensure `partitions.csv` is present and valid.
- **OTA selection**: read `otadata` to choose the correct app slot.
- **PSRAM configuration**: set PSRAM timing and mode before app loads if PSRAM is used.
- **Boot flags**: pass boot flags (safe mode, recovery, factory reset) to the app via boot parameters or NVS.
- **Manifest validation**: optional image signature verification and manifest checks.

---

## Boot manifest

A small manifest embedded in the image or stored in flash describing expected hardware and boot behavior.

**Example manifest (JSON)**

```json
{
  "version": "1.0",
  "hw_profile_hash": "sha256:...",
  "required_psram": true,
  "safe_mode_trigger_gpio": 0,
  "boot_flags": {
    "allow_ota": true,
    "require_signature": false
  }
}


