#ifndef CHRONOLINK_HAL_DISPLAY_ST7796S_H
#define CHRONOLINK_HAL_DISPLAY_ST7796S_H

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t HAL_Display_Init(void);
void HAL_Display_WriteText(const char *text);

#ifdef __cplusplus
}
#endif

#endif
