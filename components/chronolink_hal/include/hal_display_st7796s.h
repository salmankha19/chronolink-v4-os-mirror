/*
 * hal_display_st7796s.h
 *
 * ChronoLink V4 OS — Hardware Abstraction Layer
 * ST7796S 320x480 TFT LCD Display Driver (Header)
 *
 * This header exposes the public HIL API for the ST7796S display backend.
 * All functions here are implemented in hal_display_st7796s.c.
 *
 * Features:
 *  - DMA-safe SPI operations
 *  - MADCTL orientation control (low-level)
 *  - Pixel, fill, clear, and show operations
 *  - Capability reporting for HAL router
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "hal_display.h"
#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Panel dimensions (override via ST7796S_WIDTH/ST7796S_HEIGHT defines) */
#ifndef ST7796S_WIDTH
#define ST7796S_WIDTH  320
#endif

#ifndef ST7796S_HEIGHT
#define ST7796S_HEIGHT 480
#endif

/* --------------------------------------------------------------------------
 * Public HIL API
 * -------------------------------------------------------------------------- */

hal_status_t HAL_Display_ST7796S_Init(void);
hal_status_t HAL_Display_ST7796S_Deinit(void);

hal_status_t HAL_Display_ST7796S_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_ST7796S_Fill(uint32_t color);
hal_status_t HAL_Display_ST7796S_Clear(void);
hal_status_t HAL_Display_ST7796S_Show(void);

hal_status_t HAL_Display_ST7796S_WriteText(const char *text);

/* Raw MADCTL register write (low-level orientation control) */
hal_status_t HAL_Display_ST7796S_SetMadctl(uint8_t madctl);

/* Capability query */
hal_status_t HAL_Display_ST7796S_HasCapability(hal_display_cap_t cap);

#ifdef __cplusplus
}
#endif
