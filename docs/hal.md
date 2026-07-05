---

### docs/hal.md

```markdown
# HAL Layer Reference

## Purpose

This document defines the HAL responsibilities, API, memory strategy, PSRAM handling, bus initialisation, device registry, and health check flow for ChronoLink V4 OS on the ESP32‑S3‑WROOM‑1‑N16R2.

---

## HAL responsibilities

- Initialise CPU clock and frequency.
- Initialise memory and PSRAM.
- Initialise bus controllers (I2C, SPI, UART) and GPIO.
- Mount NVS and filesystem.
- Load hardware profile (`hw_profile.json`) and configuration.
- Register drivers into a thread‑safe device registry.
- Run startup health checks and post events to the State Manager.
- Signal readiness to the kernel to start the Service Layer.

---

## Recommended HAL API

```c
// hal.h
typedef enum { HAL_OK = 0, HAL_ERR } hal_status_t;

hal_status_t HAL_Init(void);
hal_status_t HAL_MemoryInit(void);
hal_status_t HAL_BusInit(void);
hal_status_t HAL_LoadConfig(void);
hal_status_t HAL_RegisterDevice(const char* name, const char* type, void* ops);
hal_status_t HAL_HealthCheck(void);
hal_status_t HAL_Ready(void);

