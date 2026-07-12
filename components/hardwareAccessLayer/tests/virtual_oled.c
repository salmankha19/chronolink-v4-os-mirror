/* virtual_oled.c
   Virtual display driver that accepts framebuffer writes and prints a short summary.
*/

#include "hal_driver_ext.h"
#include <stdio.h>
#include <string.h>

static hal_config_t _cfg;
static int _inited = 0;

static hal_status_t voled_init(const hal_config_t *cfg) {
    if (!cfg) return HAL_ERR;
    _cfg = *cfg;
    _inited = 1;
    return HAL_OK;
}

static hal_status_t voled_deinit(void) {
    _inited = 0;
    return HAL_OK;
}

static hal_status_t voled_read(void *out) {
    (void)out;
    return HAL_UNSUPPORTED;
}

static hal_status_t voled_write(const void *data, size_t len) {
    if (!_inited) return HAL_ERR;
    const uint8_t *b = (const uint8_t*)data;
    printf("[virtual_oled] received framebuffer len=%zu first=0x%02X\n", len, len ? b[0] : 0);
    return HAL_OK;
}

static hal_status_t voled_get_health(sensor_health_t *out) {
    if (!out) return HAL_ERR;
    *out = _inited ? SENSOR_HEALTH_OK : SENSOR_HEALTH_FAIL;
    return HAL_OK;
}

static hal_status_t voled_get_caps(device_caps_t *out) {
    if (!out) return HAL_ERR;
    out->type = DEV_TYPE_DISPLAY;
    out->caps.display.width = 128;
    out->caps.display.height = 64;
    out->caps.display.supports_color = false;
    out->caps.display.supports_partial_update = true;
    out->caps.display.supports_rotation = false;
    return HAL_OK;
}

const hal_driver_t VIRTUAL_OLED_DRIVER = {
    .driver_name = "virtual_oled",
    .driver_version = "0.1",
    .device_type = DEV_TYPE_DISPLAY,
    .init = voled_init,
    .deinit = voled_deinit,
    .read = voled_read,
    .write = voled_write,
    .get_health = voled_get_health,
    .get_capabilities = voled_get_caps,
    .ext_api = { .raw = NULL }
};