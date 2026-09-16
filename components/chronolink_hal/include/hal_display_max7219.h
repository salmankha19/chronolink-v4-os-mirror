#ifndef CHRONOLINK_HAL_DISPLAY_MAX7219_H
#define CHRONOLINK_HAL_DISPLAY_MAX7219_H

#include "hal_display.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public HDL API for MAX7219 backend */
hal_status_t HAL_Display_MAX7219_Init(void);
hal_status_t HAL_Display_MAX7219_WriteText(const char *text);
hal_status_t HAL_Display_MAX7219_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_MAX7219_Clear(void);
hal_status_t HAL_Display_MAX7219_Fill(uint32_t color);
hal_status_t HAL_Display_MAX7219_Show(void);

/* Capability query */
hal_status_t HAL_Display_MAX7219_HasCapability(hal_display_cap_t cap);

hal_status_t HAL_Display_MAX7219_Deinit(void);
hal_status_t HAL_Display_MAX7219_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
hal_status_t HAL_Display_MAX7219_BlitRow(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len);

/* Panel dimensions (single-module default; override at app layer if known) */
int HAL_Display_MAX7219_GetWidth(void);
int HAL_Display_MAX7219_GetHeight(void);

/* Ops table exported to the router */
extern const display_backend_ops_t HAL_DISPLAY_MAX7219_OPS;

#ifdef __cplusplus
}
#endif

#endif /* CHRONOLINK_HAL_DISPLAY_MAX7219_H */