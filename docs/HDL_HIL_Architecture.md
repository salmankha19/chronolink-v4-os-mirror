##### HDL HIL ArchitectureHDL HIL Architecture
This document describes the Hardware Dependent Layer (HDL) and the Hardware Independent Layer (HIL) for ChronoLink OS. It is a copy‑ready reference you can commit to `docs/architecture/hdlhil.md.` It includes responsibilities, interfaces, capability model, driver registration flow, configuration schema, example drivers, packaging options, and a contributor checklist.

### OverviewOverview
ChronoLink OS uses a strict layered architecture that separates hardware code from OS logic and application UI.

    Application Layer
      ↓
    OS Service Layer
      ↓
    HIL Hardware Independent Layer  ←─ Driver Registry, Config Loader, Capability Manager
      ↓
    HDL Hardware Dependent Layer    ←─ Compiled drivers or driver packs
      ↓
    Physical Hardware               ←─ I2C SPI UART GPIO devices

**Key points**
- **HDL =** concrete drivers; hardware dependent; compiled into firmware or delivered as driver packs.
- **HIL =** driver manager; part of the OS; hardware independent; exposes unified APIs to OS.
- **OS** uses HIL APIs only; Application uses OS APIs only.

Users register devices via configuration; HIL maps devices to HDL drivers.

### Goals
- Separation of concerns: keep hardware code in HDL and OS logic in HIL/Service Layer.
- Pluggable drivers: allow devices to be selected by configuration or small driver packs without recompiling OS.
- Minimal runtime footprint: support mandatory drivers in firmware and optional drivers via config or driver packs.
- Stable OS API: expose role based APIs to the Service Layer so applications remain hardware agnostic.
- Capability discovery: let drivers advertise extended features so OS can enable optional functionality.

### HDL Hardware Dependent Layer
**Role**  
HDL contains concrete drivers that implement bus transactions and device specifics.

**What HDL contains**
- Sensor drivers (temperature, humidity, pressure, CO₂, light, motion)
- Display drivers (I2C OLED, SPI OLED, MAX7219, character LCD)
- RTC, storage, audio, and other device drivers

**Driver unit model**
- One driver per device or one driver per family depending on project tradeoffs.
- Drivers are independent components compiled into firmware or packaged as driver packs.

**Driver responsibilities**
- Manage I2C SPI UART GPIO transactions.
- Convert raw registers to normalized values.
- Report health and capabilities.
- Implement nonblocking and RTOS safe behavior.
- Expose vendor features via `ext_api`.

#### HIL Hardware Independent Layer
## Role  
HIL is the OS driver manager and device selector. It exposes unified HAL APIs to the OS and orchestrates HDL drivers.

## Core components
- Config Loader reads `devices.yaml` from SPIFFS.
- Driver Registry maps logical roles to `hal_driver_t` instances.
- Probe and Activation optionally probes buses for autodetect.
- Capability Manager queries drivers for extended features.
- Health Monitor polls drivers and publishes events to the OS event bus.

**Public HIL API examples**
```c
hal_status_t hal_register_driver(const char *role, const hal_driver_t *drv, const hal_config_t *cfg);
const hal_driver_t *hal_get_driver_for_role(const char *role);
hal_status_t hal_read_role(const char *role, sensor_reading_t *out);
hal_status_t hal_display_frame(const char *role, const frame_t *frame);
hal_status_t hal_get_role_caps(const char *role, device_caps_t *out);
```

**Behavior**
- On boot HIL loads configuration then matches roles to available HDL drivers.
- HIL calls init on matched drivers and marks roles active.
- If a mandatory role fails, HIL triggers safe mode and reports health.
- HIL exposes logical role names such as temperature, light, co2, display.

### Driver Interface SpecificationDriver Interface Specification
Place these headers in docs/drivers/ and use them as the canonical interface for all HDL drivers.

**hal_driver_ext.h**
```c
#ifndef HAL_DRIVER_EXT_H
#define HAL_DRIVER_EXT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { HAL_OK = 0, HAL_ERR = -1, HAL_UNSUPPORTED = -2 } hal_status_t;

typedef enum {
    DEV_TYPE_UNKNOWN = 0,
    DEV_TYPE_SENSOR,
    DEV_TYPE_DISPLAY,
    DEV_TYPE_RTC,
    DEV_TYPE_STORAGE,
    DEV_TYPE_OTHER
} hal_device_type_t;

typedef enum {
    BUS_UNKNOWN = 0,
    BUS_I2C,
    BUS_SPI,
    BUS_UART,
    BUS_GPIO
} hal_bus_type_t;

typedef struct {
    const char *name;
    hal_bus_type_t bus;
    uint8_t bus_id;
    uint8_t i2c_addr;
    uint8_t spi_cs_pin;
    uint32_t flags;
    void *platform_data;
} hal_config_t;

typedef struct {
    float value;
    uint64_t timestamp_ms;
    int32_t raw;
} sensor_reading_t;

typedef enum { SENSOR_HEALTH_OK = 0, SENSOR_HEALTH_WARN, SENSOR_HEALTH_FAIL } sensor_health_t;

typedef struct {
    bool temperature;
    bool humidity;
    bool pressure;
    bool lux;
    bool co2;
    bool motion;
    uint32_t reserved;
} sensor_caps_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    bool supports_color;
    bool supports_partial_update;
    bool supports_rotation;
    uint32_t reserved;
} display_caps_t;

typedef struct {
    bool supports_filesystem;
    uint32_t max_size_kb;
} storage_caps_t;

typedef union {
    sensor_caps_t sensor;
    display_caps_t display;
    storage_caps_t storage;
    uint8_t raw[64];
} device_caps_u;

typedef struct {
    hal_device_type_t type;
    device_caps_u caps;
} device_caps_t;

typedef struct display_ext_api {
    hal_status_t (*draw_bitmap)(const void *bmp, size_t len);
    hal_status_t (*set_contrast)(int level);
} display_ext_api_t;

typedef struct sensor_ext_api {
    hal_status_t (*set_oversampling)(int os);
    hal_status_t (*start_continuous)(void);
} sensor_ext_api_t;

typedef union {
    display_ext_api_t *display;
    sensor_ext_api_t *sensor;
    void *raw;
} hal_ext_api_u;

typedef struct hal_driver {
    const char *driver_name;
    const char *driver_version;
    hal_device_type_t device_type;
    hal_status_t (*init)(const hal_config_t *cfg);
    hal_status_t (*deinit)(void);
    hal_status_t (*read)(void *out);
    hal_status_t (*write)(const void *data, size_t len);
    hal_status_t (*get_health)(sensor_health_t *out);
    hal_status_t (*get_capabilities)(device_caps_t *out);
    hal_ext_api_u ext_api;
} hal_driver_t;

#endif /* HAL_DRIVER_EXT_H */

```

### Capability ModelCapability Model
**Design**
- Drivers return a `device_caps_t` that includes `device_type` and a union of per‑class capability structs.
- HIL queries `get_capabilities` at registration and uses the returned `type` to interpret the union.

**Example usage**
```c
device_caps_t caps;
drv->get_capabilities(&caps);
if (caps.type == DEV_TYPE_DISPLAY) {
    display_caps_t *d = &caps.caps.display;
    /* configure framebuffer size and partial update path */
} else if (caps.type == DEV_TYPE_SENSOR) {
    sensor_caps_t *s = &caps.caps.sensor;
    /* enable temperature/humidity sampling as appropriate */
}
```
**Best practice**

- Set `device_type` first, then fill the union.
- Keep the union size conservative to allow future growth.
- Use `ext_api` for vendor specific features.

#### Driver Registration FlowDriver Registration Flow
## High level flowHigh level flow
**1) Firmware build**
- Mandatory drivers compiled into HDL.
- Optional drivers compiled as needed or provided as driver packs.

**2) User configuration**
- User edits devices.yaml in SPIFFS to declare devices and addresses.

**3) Boot HIL**
- HIL loads `devices.yaml`.
- For each device HIL finds the compiled driver by `driver_name`.
- HIL calls `drv->init(cfg)` and `drv->get_capabilities()`.
- HIL registers the driver for the logical role via `hal_register_driver(role, drv, cfg)`.

**4) Runtime**
- OS calls HIL unified APIs (e.g., `os_get_temperature()` → `hal_read_role("temperature")`).
- HIL forwards to the registered HDL driver.
- HIL monitors health and emits events.

## Failure handling
- If a mandatory role fails to initialize, HIL triggers safe mode and reports error.
- If an optional role fails, HIL logs and continues; OS shows placeholders.

## devices.yaml Schema
```yaml
devices:
  temperature:
    driver: bme280
    address: 0x76
    bus: 1
  light:
    driver: bh1750
    address: 0x23
    bus: 1
  co2:
    driver: scd41
    address: 0x62
    bus: 1
  display:
    driver: oled_i2c
    address: 0x3C
    bus: 1
```
**Notes**
- `driver` must match the `driver_name` exported by the HDL driver.
- HIL supports both config driven and probe driven registration.
- Add optional fields such as `irq_pin`, `reset_pin`, or `platform_data` as needed.

## Example DriversExample Drivers
Two example drivers are provided in `docs/drivers/skeletons`:

- `hal_oled_i2c.c` I2C OLED display driver implementing `DEV_TYPE_DISPLAY` and display_ext_api.
- `hal_scd41.c` CO₂ sensor driver implementing `DEV_TYPE_SENSOR` and `sensor_ext_api`.

These examples show how to implement `init`, `read`/`write`, `get_capabilities`, `get_health`, and `ext_api`.

### Packaging OptionsPackaging Options

| Option  | Description  |  When to use |
| ------------ | ------------ | ------------ |
| Minimal firmware  | Compile only mandatory drivers into image  | Flash constrained devices  |
| Feature firmware  | Include common optional drivers  | Devices with moderate flash  |
| Driver packs  | Optional drivers as object blobs uploaded to SPIFFS  | Devices that support runtime loading |

**Driver pack notes**
- Driver packs require a loader or simple symbol mapping.
- Validate signatures before activation.
- If runtime linking is not available, driver packs can be applied via firmware update.

### HIL Implementation NotesHIL Implementation Notes
- Support both config driven and probe driven activation.
- Provide a small CLI or web UI to edit `devices.yaml.`
- Emit events `EVENT_DRIVER_REGISTERED`, `EVENT_DRIVER_FAILED`, `EVENT_CAPABILITY_CHANGED`.
- Poll get_health periodically and publish health events.
- Keep HIL free of hardware specifics; it only calls HDL driver function pointers.

### Best Practices
- Keep the common interface small and stable.
- Use `ext_api` for vendor features.
- Prefer nonblocking and RTOS safe implementations.
- Document each driver: `driver_name`, `device_type`, capability layout, ext_api usage.
- Provide virtual drivers for CI and unit tests.
- Validate driver packs and configuration signatures before activation.
- Add unit and integration tests that boot with different `devices.yaml` variants.

### Contributor Checklist for New DriversContributor Checklist for New Drivers
- [ ] Add driver source to `drivers/` with filename `hal_<name>.c`.
- [ ] Implement `hal_driver_t` and export a `const hal_driver_t <NAME>_DRIVER`.
- [ ] Implement `init`, `deinit`, `read` or `write`, `get_health`, `get_capabilities`.
- [ ] Fill `device_type` and return correct `device_caps_t`.
- [ ] Provide `ext_api` if vendor features exist and document it.
- [ ] Ensure driver is FreeRTOS safe and nonblocking where possible.
- [ ] Add unit tests and a virtual driver for CI.
- [ ] Add documentation entry in `docs/drivers/` describing usage and config.
- [ ] Add example `devices.yaml` entry to `docs/config/`.

## Example HIL Loader Pseudocode
    hal_status_t hil_boot(void) {
        hal_status_t s;
        s = hal_init_registry();
        if (s != HAL_OK) return s;
    
        config_t *cfg = config_load("/spiffs/devices.yaml");
        for each entry in cfg->devices {
            const char *role = entry.key;
            const char *driver_name = entry.driver;
            hal_config_t c = {0};
            c.name = role;
            c.bus = entry.bus;
            c.bus_id = entry.bus;
            c.i2c_addr = entry.address;
    
            const hal_driver_t *drv = find_compiled_driver(driver_name);
            if (!drv) {
                log_warn("driver %s not found for role %s", driver_name, role);
                continue;
            }
    
            if (hal_register_driver(role, drv, &c) == HAL_OK) {
                device_caps_t caps;
                if (drv->get_capabilities && drv->get_capabilities(&caps) == HAL_OK) {
                    publish_caps_to_os(role, &caps);
                }
            } else {
                log_error("failed to register %s for role %s", driver_name, role);
            }
        }
    
        probe_and_autoregister();
        return HAL_OK;
    }
	
### Testing and ValidationTesting and Validation
- Implement virtual drivers for unit tests.
- Add integration tests that boot with multiple devices.yaml variants.
- Validate `get_capabilities` and `get_health` behavior.
- Test driver pack loading and signature validation if supported.

## Recommended Repo Layout
    docs/
      architecture/
        hdlhil.md
      drivers/
        hal_driver_ext.h
        hal_driver_caps.h
        skeletons/
          hal_bme280.c
          hal_oled_i2c.c
          hal_scd41.c
      config/
        devices.yaml.example
    src/
      hal/
        drivers/
        hil/
        registry/

### Next StepsNext Steps
- Commit this file to `docs/architecture/hdlhil.md`.
- Add `hal_driver_ext.h` and `hal_driver_caps.h` to` docs/drivers/`.
- Add the example driver skeletons to `docs/drivers/skeletons/`.
- Implement `hal_registry.c` reference implementation and platform helper stubs for your target SDK.
- Add a short HIL boot flow diagram to the architecture docs.