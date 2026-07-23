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
 * Public HIL API (only these should be visible to other modules)
 * -------------------------------------------------------------------------- */

hal_status_t HAL_Display_ST7796S_Init(void);
hal_status_t HAL_Display_ST7796S_Deinit(void);

hal_status_t HAL_Display_ST7796S_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_ST7796S_Fill(uint32_t color);
hal_status_t HAL_Display_ST7796S_Clear(void);
hal_status_t HAL_Display_ST7796S_Show(void);

hal_status_t HAL_Display_ST7796S_WriteText(const char *text);

/* Orientation control */
hal_status_t HAL_Display_ST7796S_SetOrientation(uint8_t madctl);

/* Capability query */
hal_status_t HAL_Display_ST7796S_HasCapability(hal_display_cap_t cap);

#ifdef __cplusplus
}
#endif