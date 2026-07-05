#ifndef CHRONOLINK_HAL_DISPLAY_H
#define CHRONOLINK_HAL_DISPLAY_H

#include "hal.h"

hal_status_t HAL_Display_Init(void);
void HAL_Display_WriteText(const char *text);

#endif