#ifndef HAL_DRIVER_EXT_H
#define HAL_DRIVER_EXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HAL_OK = 0,
    HAL_ERR = -1,
    HAL_UNSUPPORTED = -2,
    HAL_NOT_FOUND = -3,
} hal_status_t;

typedef enum {
    DEV_TYPE_UNKNOWN = 0,
    DEV_TYPE_SENSOR,
    DEV_TYPE_DISPLAY,
    DEV_TYPE_RTC,
    DEV_TYPE_STORAGE,
    DEV_TYPE_OTHER,
} hal_device_type_t;

typedef enum {
    BUS_UNKNOWN = 0,
    BUS_I2C,
    BUS_SPI,
    BUS_UART,
    BUS_GPIO,
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

typedef enum {
    SENSOR_HEALTH_OK = 0,
    SENSOR_HEALTH_WARN,
    SENSOR_HEALTH_FAIL,
} sensor_health_t;

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
