                ┌───────────────────────────────────────────┐
                │        Application Layer (UI)            												 │
                │  - ui_show_temperature()                 											 │
                │  - ui_show_light()                       												    │
                │  - ui_show_co2()                        												    │
                └───────────────────────────────▲───────────┘
                                                											│
                                                											│ OS APIs
                                                											│
                ┌────────────────────────────────▼──────────┐
                │           OS Service Layer                 												 │
                │  - os_get_temperature()                   											  │
                │  - os_get_light_level()                   												  │
                │  - os_get_co2()                           													│
                │  - os_display_frame()                     												 │
                └───────────────────────────────▲───────────┘
                                                											│
                                                											│ Unified HAL APIs
                                                											│
                ┌───────────────────────────────▼──────────┐
                │   HIL: Hardware Independent Layer          │
                │  (Driver Manager / Device Selector)        │
                │                                            │
                │  1. Load config file from SPIFFS           │
                │     devices:                               │
                │       temperature: bme280                  │
                │       light: bh1750                        │
                │       co2: scd41                           │
                │                                            │
                │  2. Match device names to HDL drivers      │
                │     - find_driver("bme280")                │
                │     - find_driver("bh1750")                │
                │     - find_driver("scd41")                 │
                │                                            │
                │  3. Register drivers                       │
                │     hal_register_driver("temperature", BME280_DRIVER)
                │     hal_register_driver("light", BH1750_DRIVER)
                │     hal_register_driver("co2", SCD41_DRIVER)
                │                                            │
                │  4. Expose unified APIs to OS              │
                │     - hal_read("temperature")              │
                │     - hal_read("light")                    │
                │     - hal_read("co2")                      │
                └───────────────────────────────▲──────────┘
                                                │
                                                │ Driver Interface Calls
                                                │
                ┌────────────────────────────────▼──────────┐
                │   HDL: Hardware Dependent Layer            									│
                │   (Drivers Folder)                         												   │
                │                                            															│
                │   BME280_DRIVER:                           											  │
                │     - init()                               														  │
                │     - read()                               														│
                │     - get_health()                         													│
                │     - get_capabilities()                   												  │
                │                                            															│
                │   BH1750_DRIVER:                           											   │
                │     - init()                               														  │
                │     - read()                               														│
                │     - get_health()                         													│
                │     - get_capabilities()                   												  │
                │                                            															│
                │   SCD41_DRIVER:                            												│
                │     - init()                               														  │
                │     - read()                               														│
                │     - get_health()                         													│
                │     - get_capabilities()                   												  │
                │                                            															│
                └──────────────────────▲────────────────────┘
                                                						│
                                                						│ I2C/SPI Hardware Access
                                                						│
                ┌──────────────────────▼────────────────────┐
                │               Physical Hardware            												│
                │   - BME280 Temperature Sensor              									  │
                │   - BH1750 Light Sensor                    											 │
                │   - SCD41 CO₂ Sensor                       											 │
                │   - I2C Display / MAX7219 / OLED           									  │
                └────────────────────────────────────────────┘
