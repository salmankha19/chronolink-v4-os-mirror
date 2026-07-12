#ifndef CHRONOLINK_HAL_DISPLAY_MAX7219_H
#define CHRONOLINK_HAL_DISPLAY_MAX7219_H

#include "hal_display.h"
#include <stdint.h>

/* Public HDL API for MAX7219 backend */
hal_status_t HAL_Display_MAX7219_Init(void);
hal_status_t HAL_Display_MAX7219_WriteText(const char *text);
hal_status_t HAL_Display_MAX7219_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_MAX7219_Clear(void);
hal_status_t HAL_Display_MAX7219_Fill(uint32_t color);
hal_status_t HAL_Display_MAX7219_Show(void);

/* Capability query */
hal_status_t HAL_Display_MAX7219_HasCapability(hal_display_cap_t cap);

#endif /* CHRONOLINK_HAL_DISPLAY_MAX7219_H */