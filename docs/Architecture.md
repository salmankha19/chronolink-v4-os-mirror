# ChronoLink V4 — System Architecture

ChronoLink V4 uses a clean 4‑layer architecture:

1. **PDL Board Layer** — board profile, pin macros, partition notes  
2. **PIL / HAL Layer** — stable C ABI + MCU‑specific drivers  
3. **Service / Engine Layer** — pure logic (state, prayer engine, scheduler, events)  
4. **Application Layer** — UI, display cycle, web config

---

```mermaid
flowchart TB

%% ============================
%% APPLICATION LAYER
%% ============================
subgraph APP["Application Layer"]
    CLOCK_APP["Clock App"]
    WEB_CONFIG["Web Config App"]
    DISPLAY_CYCLE["Display Cycle"]
    THEME_ENGINE["Theme Engine"]
    ERROR_UI["Error Display"]
end

%% ============================
%% SERVICE / ENGINE LAYER
%% ============================
subgraph SERVICE["Service / Engine Layer"]
    STATE_MANAGER["State Manager"]
    EVENT_BUS["Event Bus"]
    PRAYER_ENGINE["Prayer Engine"]
    TIME_ENGINE["Time Engine"]
    SCHEDULER["Scheduler"]
    CONFIG_SERVICE["Config Service"]
    THEME_REGISTRY["Theme Registry"]
end

%% ============================
%% PIL / HAL LAYER
%% ============================
subgraph HAL["PIL / HAL Layer"]
    PIL_GPIO["PIL GPIO"]
    PIL_I2C["PIL I2C"]
    PIL_SPI["PIL SPI"]
    PIL_SENSOR["PIL Sensor API"]
    PIL_CONFIG["PIL Config Loader"]

    HAL_GPIO["HAL GPIO Driver"]
    HAL_I2C["HAL I2C Driver"]
    HAL_SPI["HAL SPI Driver"]
    HAL_SENSOR["HAL Sensor Driver"]
    HAL_BUS["HAL Bus Manager"]
    HAL_HEALTH["HAL Health Monitor"]
end

%% ============================
%% PDL BOARD LAYER
%% ============================
subgraph PDL["PDL Board Layer"]
    BOARD_PROFILE["Board Profile"]
    PIN_MAP["Pin Map"]
    PARTITIONS["Partition Table"]
    CAPABILITIES["Board Capabilities"]
end

%% ============================
%% CONNECTIONS
%% ============================

%% Application → Service
CLOCK_APP --> STATE_MANAGER
WEB_CONFIG --> CONFIG_SERVICE
DISPLAY_CYCLE --> EVENT_BUS
THEME_ENGINE --> THEME_REGISTRY
ERROR_UI --> STATE_MANAGER

%% Service → PIL/HAL
TIME_ENGINE --> PIL_CONFIG
TIME_ENGINE --> HAL_GPIO
PRAYER_ENGINE --> PIL_CONFIG
PRAYER_ENGINE --> HAL_SENSOR
SCHEDULER --> EVENT_BUS
CONFIG_SERVICE --> PIL_CONFIG
STATE_MANAGER --> HAL_HEALTH

%% PIL → HAL
PIL_GPIO --> HAL_GPIO
PIL_I2C --> HAL_I2C
PIL_SPI --> HAL_SPI
PIL_SENSOR --> HAL_SENSOR

%% HAL → PDL
HAL_GPIO --> PIN_MAP
HAL_I2C --> PIN_MAP
HAL_SPI --> PIN_MAP
HAL_SENSOR --> CAPABILITIES
HAL_BUS --> BOARD_PROFILE
HAL_HEALTH --> BOARD_PROFILE

