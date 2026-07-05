# Copilot Agent Rules for ChronoLink V4 OS

Copilot Agent must follow all rules defined in:

- `/docs/CHRONOLINK_CONSTITUTION.md`
- `/docs/Architecture.md`
- `/docs/boot.md`
- `/docs/hal.md`
- `/partitions.csv`

These documents define the architecture, boot sequence, HAL responsibilities, filesystem rules, and component boundaries for ChronoLink V4 OS.

### Mandatory Agent Behavior

- Follow the 6‑layer architecture strictly  
- Respect boot sequence ordering  
- Use FATFS only (never SPIFFS or LittleFS)  
- Use valid ESP‑IDF 6 component names  
- Avoid circular dependencies  
- Validate CMakeLists.txt correctness  
- Validate PlatformIO compatibility  
- Validate header resolution  
- Never generate code outside architecture boundaries  
- Never introduce unsupported components  

### Filesystem Enforcement

Only FATFS is allowed.  
Forbidden: SPIFFS, esp_spiffs, spiffs, esp_littlefs, littlefs, vfs_littlefs.

### Boot Manager Enforcement

`boot_manager.c` must only initialize NVS, FATFS, OTA, logging, and hand off to `core_os`.

### Kernel Enforcement

`core_os` must only manage tasks, queues, timers, and dual‑core scheduling.

### HAL Enforcement

HAL must only contain hardware access and expose clean APIs.

### Services Enforcement

Services must only contain logic engines and use HAL APIs.

### Application Enforcement

Application must only contain UI and display logic.

---

Copilot Agent must comply with these rules for all fixes, refactors, and code generation.

