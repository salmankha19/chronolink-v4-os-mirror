# ChronoLink V4 OS

**ChronoLink V4 OS** — HAL, boot, and partition reference for ESP32‑S3‑WROOM‑1‑N16R2.

This repository contains the HAL and boot documentation, partition table, and example configuration used by ChronoLink V4 OS. Use these files as the canonical reference for implementing the bootloader, HAL, and service startup sequence.

## Quick links

- **Boot documentation**: `docs/boot.md`  
- **HAL documentation**: `docs/hal.md`  
- **Partition table**: `partitions.csv`  
- **Example hardware profile**: `assets/hw_profile.json`  
- **PSRAM linker snippet**: `components/ld/psram.ld`

## Quick Start

Prerequisites:

- **ESP‑IDF** (recommended version documented in `CONTRIBUTING.md`)  
- Python 3.x and `idf.py` toolchain

Build and flash:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash