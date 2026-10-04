# HAL and PDL Reference

Covers `components/chronolink_hal` (the HAL, which also contains the
PDL headers) and `components/chronolink_driver_sdk`.

Everything here describes the code as it is. Planned work is in
[STATUS.md](STATUS.md), not here. If a statement disagrees with the
code, the code wins — fix the document.

---

## 1. Responsibilities

**PDL (Platform/Board Definition)** — pin numbers, board identity,
capability flags, pin validation, initial pin direction and level.

Files: `pdl_pins.h`, `pdl_capabilities.h`, `pdl_partitions.h`,
`pdl_board.h`, `pdl_compat.h`, `board_pins.h`, `pdl_board_esp32.c`.

**HAL (Hardware Abstraction Layer)** — drivers that touch hardware.
Never calls services, kernel, or application code.

The HAL does **not** currently do memory / PSRAM setup, hardware-profile
loading, a general device registry (other than sensors), or health
checks. Earlier docs listed `HAL_MemoryInit`, `HAL_BusInit`,
`HAL_LoadConfig`, `HAL_RegisterDevice`, `HAL_HealthCheck`, `HAL_Ready`.
**None of these exist.**

---

## 2. Status codes

```c
typedef enum {
    HAL_OK        =  0,
    HAL_ERR_INIT  = -1,   /* init failed, not initialised, or resource/slot problem */
    HAL_ERR_BUS   = -2,   /* bus busy / lock timeout / transfer timeout */
    HAL_ERR_DEV   = -3    /* device failure, invalid argument, or "not supported" */
} hal_status_t;