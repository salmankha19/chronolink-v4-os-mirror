/*
 * hal_display.h
 *
 * ChronoLink V4 OS — Hardware Abstraction Layer
 * Display Router Public Interface
 *
 * This header defines the backend-agnostic API consumed by application
 * code and upper layers (graphics, UI shell). The implementation in
 * hal_display.c routes calls to the active backend selected at compile
 * time via Kconfig or board configuration.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Capability flags
 *
 * Backends return HAL_OK for capabilities they support, HAL_ERR_DEV for
 * those they do not. The router validates the cap value before dispatch.
 * -------------------------------------------------------------------------- */
typedef enum {
    HAL_CAP_DRAW_PIXEL = 0,
    HAL_CAP_FILL,
    HAL_CAP_CLEAR,
    HAL_CAP_SHOW,
    HAL_CAP_TEXT,          /* Character/text output */
    HAL_CAP_BITMAP,        /* Buffered bitmap/blitting */
    HAL_CAP_BRIGHTNESS,    /* PWM or DAC brightness control */
    HAL_CAP_ORIENTATION,   /* MADCTL / SetMadctl support */
    HAL_CAP_COUNT          /* Keep last; not a valid capability */
} hal_display_cap_t;

/* --------------------------------------------------------------------------
 * Backend identifiers (for diagnostics and runtime inspection)
 * -------------------------------------------------------------------------- */
typedef enum {
    HAL_DISPLAY_BACKEND_NONE = 0,
    HAL_DISPLAY_BACKEND_ST7796S,
    HAL_DISPLAY_BACKEND_MAX7219,
    HAL_DISPLAY_BACKEND_REMOTE,
    HAL_DISPLAY_BACKEND_COUNT
} hal_display_backend_t;

/* --------------------------------------------------------------------------
 * Public HAL Display API (backend-agnostic)
 *
 * All functions return HAL_OK on success, or an appropriate hal_status_t
 * error code. The router validates state and dispatches to the active
 * backend; backends must not be called directly from application code.
 * -------------------------------------------------------------------------- */

/**
 * @brief Initialize the display subsystem.
 *
 * Selects the backend based on Kconfig/board configuration, initializes
 * the underlying driver, and creates the display mutex.
 */
hal_status_t HAL_Display_Init(void);

/**
 * @brief Deinitialize the display subsystem.
 *
 * Turns off the panel, releases driver resources, and resets state.
 * The mutex is retained so re-init is safe.
 */
hal_status_t HAL_Display_Deinit(void);

/**
 * @brief Draw a single pixel.
 * @param x X coordinate (0..width-1)
 * @param y Y coordinate (0..height-1)
 * @param color 24-bit RGB color (0xRRGGBB)
 */
hal_status_t HAL_Display_DrawPixel(uint16_t x, uint16_t y, uint32_t color);

/**
 * @brief Fill the entire screen with a color.
 * @param color 24-bit RGB color (0xRRGGBB)
 */
hal_status_t HAL_Display_Fill(uint32_t color);

/**
 * @brief Clear the screen to black.
 */
hal_status_t HAL_Display_Clear(void);

/**
 * @brief Flush / show any pending frame buffer changes.
 *
 * For direct-write panels (like ST7796S) this is a no-op.
 * For buffered displays this commits the back-buffer.
 */
hal_status_t HAL_Display_Show(void);

hal_status_t HAL_Display_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
hal_status_t HAL_Display_BlitRow(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len);

/**
 * @brief Write text to the display.
 *
 * NOTE: The ST7796S backend returns HAL_ERR_DEV for HAL_CAP_TEXT;
 * a graphics/font layer should be used for real text rendering.
 */
hal_status_t HAL_Display_WriteText(const char *text);

/**
 * @brief Query whether the active backend supports a capability.
 * @param cap Capability to query
 * @return HAL_OK if supported, HAL_ERR_DEV if not, HAL_ERR_INIT if not initialized
 */
hal_status_t HAL_Display_HasCapability(hal_display_cap_t cap);

/**
 * @brief Low-level MADCTL register write (orientation control).
 *
 * Supported by backends that expose raw register access (e.g. ST7796S).
 * Returns HAL_ERR_DEV if the active backend does not implement it.
 *
 * @param madctl Raw MADCTL byte (see panel datasheet)
 */
hal_status_t HAL_Display_SetMadctl(uint8_t madctl);

/**
 * @brief Return the currently active backend identifier.
 *
 * Useful for diagnostics and feature-gating in upper layers.
 */
hal_display_backend_t HAL_Display_GetBackend(void);

#ifdef __cplusplus
}
#endif