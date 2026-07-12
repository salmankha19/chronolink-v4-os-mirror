#ifndef CHRONOLINK_HAL_DISPLAY_H
#define CHRONOLINK_HAL_DISPLAY_H

#include "hal.h"

/* Unified HIL API */
hal_status_t HAL_Display_Init(void);
void HAL_Display_WriteText(const char *text);

/* Driver-specific HDL APIs */
hal_status_t HAL_Display_ST7796S_Init(void);
hal_status_t HAL_Display_MAX7219_Init(void);
hal_status_t HAL_Display_Remote_Init(void);

#endif
