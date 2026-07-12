/* test_registry.c
   Simple test harness that creates a registry, registers virtual drivers,
   reads a sensor role and writes a small framebuffer to a display role.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_registry.h"
#include "hal_driver_ext.h"

/* Extern virtual drivers compiled into this test binary */
extern const hal_driver_t VIRTUAL_SHT4X_DRIVER;
extern const hal_driver_t VIRTUAL_OLED_DRIVER;

static int print_caps(const char *role, const hal_driver_t *drv, const hal_config_t *cfg, void *ctx) {
    (void)cfg; (void)ctx;
    device_caps_t caps;
    if (!drv || !drv->get_capabilities) {
        printf("role=%s: no capabilities\n", role);
        return 0;
    }
    if (drv->get_capabilities(&caps) != HAL_OK) {
        printf("role=%s: get_capabilities failed\n", role);
        return 0;
    }
    if (caps.type == DEV_TYPE_SENSOR) {
        sensor_caps_t *s = &caps.caps.sensor;
        printf("role=%s: SENSOR caps: temp=%d hum=%d pres=%d co2=%d lux=%d motion=%d\n",
               role, s->temperature, s->humidity, s->pressure, s->co2, s->lux, s->motion);
    } else if (caps.type == DEV_TYPE_DISPLAY) {
        display_caps_t *d = &caps.caps.display;
        printf("role=%s: DISPLAY caps: %ux%u color=%d partial=%d rotation=%d\n",
               role, d->width, d->height, d->supports_color, d->supports_partial_update, d->supports_rotation);
    } else {
        printf("role=%s: unknown device type %d\n", role, caps.type);
    }
    return 0;
}

int main(void) {
    printf("=== HAL Registry Test Harness ===\n");

    /* Create registry with room for 8 entries */
    hal_registry_t *reg = hal_registry_create(8);
    if (!reg) {
        fprintf(stderr, "failed to create registry\n");
        return 2;
    }

    /* Register virtual SHT4x as "temperature" (and optionally "humidity") */
    hal_config_t cfg_temp = {0};
    cfg_temp.name = "temperature";
    cfg_temp.bus = BUS_I2C;
    cfg_temp.bus_id = 1;
    cfg_temp.i2c_addr = 0x44; /* common SHT4x I2C address (0x44 or 0x45) */

    if (hal_registry_register_driver(reg, "temperature", &VIRTUAL_SHT4X_DRIVER, &cfg_temp) != HAL_OK) {
        fprintf(stderr, "failed to register temperature driver\n");
        hal_registry_destroy(reg);
        return 3;
    }

    /* Optionally register the same driver for a separate "humidity" role (if your OS expects separate roles) */
    hal_config_t cfg_hum = cfg_temp;
    cfg_hum.name = "humidity";
    if (hal_registry_register_driver(reg, "humidity", &VIRTUAL_SHT4X_DRIVER, &cfg_hum) != HAL_OK) {
        /* it's fine to skip if you don't need a separate role */
    }

    /* Register virtual OLED as "display" */
    hal_config_t cfg_disp = {0};
    cfg_disp.name = "display";
    cfg_disp.bus = BUS_I2C;
    cfg_disp.bus_id = 1;
    cfg_disp.i2c_addr = 0x3C;

    if (hal_registry_register_driver(reg, "display", &VIRTUAL_OLED_DRIVER, &cfg_disp) != HAL_OK) {
        fprintf(stderr, "failed to register display driver\n");
        hal_registry_destroy(reg);
        return 4;
    }

    /* Print capabilities for all registered roles */
    printf("\nRegistered roles and capabilities:\n");
    hal_registry_iterate(reg, print_caps, NULL);

    /* Read temperature role */
    sensor_reading_t r;
    if (hal_registry_read_role(reg, "temperature", &r) == HAL_OK) {
        printf("\nRead temperature: value=%.2f raw=%ld timestamp=%llu\n", r.value, (long)r.raw, (unsigned long long)r.timestamp_ms);
    } else {
        printf("\nFailed to read temperature role\n");
    }

    /* Write a small framebuffer to display role */
    uint8_t fb[16]; /* tiny fake framebuffer */
    for (size_t i = 0; i < sizeof(fb); ++i) fb[i] = (uint8_t)i;

    if (hal_registry_write_role(reg, "display", fb, sizeof(fb)) == HAL_OK) {
        printf("Wrote framebuffer to display role (len=%zu)\n", sizeof(fb));
    } else {
        printf("Failed to write framebuffer to display role\n");
    }

    /* Query health */
    sensor_health_t health;
    const hal_driver_t *drv = hal_registry_get_driver(reg, "temperature");
    if (drv && drv->get_health && drv->get_health(&health) == HAL_OK) {
        printf("Temperature health: %d\n", (int)health);
    }

    /* Unregister roles and destroy registry */
    hal_registry_unregister_role(reg, "temperature");
    hal_registry_unregister_role(reg, "display");
    hal_registry_destroy(reg);

    printf("=== Test complete ===\n");
    return 0;
}