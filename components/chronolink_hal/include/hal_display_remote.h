#ifndef CHRONOLINK_HAL_DISPLAY_REMOTE_H
#define CHRONOLINK_HAL_DISPLAY_REMOTE_H

#include "hal_display.h"
#include <stdint.h>

/* Public HDL API for Remote backend */
hal_status_t HAL_Display_Remote_Init(void);
hal_status_t HAL_Display_Remote_WriteText(const char *text);
hal_status_t HAL_Display_Remote_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_Remote_Clear(void);
hal_status_t HAL_Display_Remote_Fill(uint32_t color);
hal_status_t HAL_Display_Remote_Show(void);

/* Capability query */
hal_status_t HAL_Display_Remote_HasCapability(hal_display_cap_t cap);

#endif /* CHRONOLINK_HAL_DISPLAY_REMOTE_H */