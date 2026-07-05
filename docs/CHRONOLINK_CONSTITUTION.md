# ChronoLink V4 OS — Project Constitution

This document defines the mandatory architectural, boot, filesystem, and component rules for ChronoLink V4 OS. All development, fixes, refactors, and code generation must comply with this constitution.

---

## 1. Architecture Layers (Mandatory)

ChronoLink V4 OS uses a strict 6‑layer architecture:

1. Partition Table  
2. Boot Sequence (`boot_manager`)  
3. Kernel (`core_os`)  
4. HAL (drivers)  
5. Services (engines)  
6. Application (UI + logic)

### Rules
- Lower layers must never call higher layers.  
- Higher layers may only call lower layers through public APIs.  
- No circular dependencies are allowed.

---

## 2. Boot Sequence Rules (Mandatory)

`boot_manager.c` must:

- Initialize NVS first  
- Mount FATFS on `/storage`  
- Initialize OTA partition selection  
- Initialize logging  
- Call `core_os_init()` then `core_os_start()`

### Forbidden in `boot_manager.c`
- Starting tasks  
- Accessing hardware directly  
- Accessing UI or display  
- Accessing services or HAL  
- Mounting any filesystem except FATFS  

---

## 3. Filesystem Rules (Mandatory)

ESP‑IDF 6.0.1 under PlatformIO supports **only FATFS**.

### Allowed
- Component: `fatfs`  
- Header: `esp_vfs_fat.h`  
- API: `esp_vfs_fat_spiflash_mount()`  
- Partition type: `fat`  
- Partition name: `storage`  
- Base path: `/storage`

### Forbidden
- SPIFFS  
- `esp_spiffs`  
- `spiffs`  
- `esp_littlefs`  
- `littlefs`  
- `vfs_littlefs`

Never generate code referencing these.

---

## 4. CMake Rules (Mandatory)

### Valid ESP‑IDF 6 component names
- `esp_common`  
- `nvs_flash`  
- `fatfs`  
- `app_update`  
- `esp_timer`  
- `esp_event`  
- `esp_system`  
- `log` (correct component for `esp_log.h`)

### Invalid component names
- `esp_log`  
- `esp_littlefs`  
- `littlefs`  
- `vfs_littlefs`  
- `spiffs`  
- `esp_spiffs`

All components must declare `REQUIRES` using only valid names.

---

## 5. PlatformIO Rules (Mandatory)

`platformio.ini` must contain:

- `framework = espidf`  
- `board_build.filesystem = fatfs`  
- No SPIFFS or LittleFS flags  
- No references to removed components  

---

## 6. Partition Table Rules (Mandatory)

`partitions.csv` must contain:
storage,data,fat,0x820000,0x7E0000,

Rules:
- Must not overlap app partitions  
- Must not use SPIFFS or LittleFS  

---

## 7. Kernel Rules (Mandatory)

`core_os` must:

- Create FreeRTOS tasks  
- Create queues  
- Create timers  
- Manage dual‑core scheduling  
- Start core0 (system/network)  
- Start core1 (UI/clock)

### Forbidden in `core_os`
- Mounting filesystems  
- Initializing NVS  
- Accessing hardware directly  

---

## 8. HAL Rules (Mandatory)

HAL drivers must:

- Contain **only** hardware access  
- Expose clean APIs  
- Never call services or application  

---

## 9. Services Rules (Mandatory)

Services must:

- Contain logic engines (prayer engine, sensor engine, theme engine)  
- Use HAL APIs  
- Never call `boot_manager`  
- Never mount filesystems  

---

## 10. Application Rules (Mandatory)

Application must:

- Contain UI logic  
- Contain display logic  
- Use services  
- Never call HAL directly  

---

## 11. Agent Behavior Rules (Mandatory)

Copilot Agent must:

- Stay inside this architecture  
- Never generate code outside these boundaries  
- Never introduce unsupported components  
- Never propose SPIFFS or LittleFS  
- Always validate CMake component names  
- Always validate ESP‑IDF 6 compatibility  
- Always validate PlatformIO compatibility  
- Always validate boot sequence order  
- Always validate dependency graphs  
- Always validate header resolution  
- Always avoid circular dependencies  

---

## Goal

Maintain ChronoLink V4 OS architecture integrity and produce code that compiles cleanly under PlatformIO ESP‑IDF 6.0.1 with FATFS.


