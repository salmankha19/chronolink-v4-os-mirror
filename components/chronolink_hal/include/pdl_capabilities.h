#ifndef CHRONOLINK_HAL_PDL_CAPABILITIES_H
#define CHRONOLINK_HAL_PDL_CAPABILITIES_H

/* Migrated from src/platformDependentLayer/pdl_capabilities.h
   Feature flags describing available hardware on this board. */

#define PDL_HAS_RTC_DS3231M      1
#define PDL_HAS_DISPLAY_ST7796   1
#define PDL_HAS_DISPLAY_MAX7219  0
#define PDL_HAS_WIFI             1
#define PDL_HAS_BLE              0

#define PDL_HAS_SENSOR_SHT4X     1
#define PDL_HAS_SENSOR_BME280    0
#define PDL_HAS_SENSOR_VEML7700  1
#define PDL_HAS_AUDIO_MAX98357A  1

/* Use 1 to enable, 0 to disable. Adjust to match your BOM. */
#endif /* CHRONOLINK_HAL_PDL_CAPABILITIES_H */
