#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "hal_display.h"
#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Custom display driver vtable
 *
 * A driver outside chronolink_hal fills this table and registers it.
 * The router will call these instead of the built-in backend when
 * the custom backend is selected.
 * -------------------------------------------------------------------------- */
typedef struct {
    const char *name;   /* human-readable driver name */

    hal_status_t (*init)(void);
    hal_status_t (*deinit)(void);

    hal_status_t (*draw_pixel)(uint16_t x, uint16_t y, uint32_t color);
    hal_status_t (*fill)(uint32_t color);
    hal_status_t (*fill_rect)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
    hal_status_t (*blit_row)(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len);
    hal_status_t (*clear)(void);
    hal_status_t (*show)(void);
    hal_status_t (*write_text)(const char *text);

    hal_status_t (*has_capability)(hal_display_cap_t cap);
    hal_status_t (*set_madctl)(uint8_t madctl);
    hal_status_t (*set_scroll_area)(uint16_t tfa, uint16_t vsa, uint16_t bfa);
    hal_status_t (*set_scroll_start)(uint16_t vss);

    int (*get_width)(void);
    int (*get_height)(void);
} chronolink_display_driver_t;

/* --------------------------------------------------------------------------
 * Registration API
 * -------------------------------------------------------------------------- */

/**
 * @brief Register a custom display driver.
 *
 * Call once during system init (before HAL_Display_Init).
 * Only ONE custom driver can be registered; subsequent calls overwrite.
 *
 * @param drv Pointer to populated driver vtable. Must remain valid
 *            for the lifetime of the application (static storage recommended).
 * @return HAL_OK on success, HAL_ERR_INIT if drv is NULL.
 */
hal_status_t chronolink_driver_register(const chronolink_display_driver_t *drv);

/**
 * @brief Unregister the custom driver.
 */
void chronolink_driver_unregister(void);

/**
 * @brief Query whether a custom driver has been registered.
 */
bool chronolink_driver_is_registered(void);

/**
 * @brief Get the currently registered custom driver (or NULL).
 */
const chronolink_display_driver_t *chronolink_driver_get(void);

#ifdef __cplusplus
}
#endif
