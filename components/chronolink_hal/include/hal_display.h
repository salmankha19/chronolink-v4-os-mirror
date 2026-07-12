#ifndef CHRONOLINK_HAL_DISPLAY_H
#define CHRONOLINK_HAL_DISPLAY_H

#include "hal.h"

/* Unified HIL API */
hal_status_t HAL_Display_Init(void);
hal_status_t HAL_Display_WriteText(const char *text);
hal_status_t HAL_Display_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_Clear(void);
hal_status_t HAL_Display_Fill(uint32_t color);
hal_status_t HAL_Display_Show(void);

/* Driver-specific HDL APIs */
hal_status_t HAL_Display_ST7796S_Init(void);
hal_status_t HAL_Display_ST7796S_WriteText(const char *text);
hal_status_t HAL_Display_ST7796S_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_ST7796S_Clear(void);
hal_status_t HAL_Display_ST7796S_Fill(uint32_t color);
hal_status_t HAL_Display_ST7796S_Show(void);

hal_status_t HAL_Display_MAX7219_Init(void);
hal_status_t HAL_Display_MAX7219_WriteText(const char *text);
hal_status_t HAL_Display_MAX7219_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_MAX7219_Clear(void);
hal_status_t HAL_Display_MAX7219_Fill(uint32_t color);
hal_status_t HAL_Display_MAX7219_Show(void);

hal_status_t HAL_Display_Remote_Init(void);
hal_status_t HAL_Display_Remote_WriteText(const char *text);
hal_status_t HAL_Display_Remote_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_Remote_Clear(void);
hal_status_t HAL_Display_Remote_Fill(uint32_t color);
hal_status_t HAL_Display_Remote_Show(void);

#endif
