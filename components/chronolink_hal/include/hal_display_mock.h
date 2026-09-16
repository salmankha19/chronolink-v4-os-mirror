#ifndef CHRONOLINK_HAL_DISPLAY_MOCK_H
#define CHRONOLINK_HAL_DISPLAY_MOCK_H

#include "hal_display.h"

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t HAL_Display_Mock_Init(void);
hal_status_t HAL_Display_Mock_Deinit(void);
hal_status_t HAL_Display_Mock_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_Mock_Fill(uint32_t color);
hal_status_t HAL_Display_Mock_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
hal_status_t HAL_Display_Mock_BlitRow(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len);
hal_status_t HAL_Display_Mock_Clear(void);
hal_status_t HAL_Display_Mock_Show(void);
hal_status_t HAL_Display_Mock_WriteText(const char *text);
hal_status_t HAL_Display_Mock_HasCapability(hal_display_cap_t cap);
hal_status_t HAL_Display_Mock_SetMadctl(uint8_t madctl);
hal_status_t HAL_Display_Mock_SetScrollArea(uint16_t tfa, uint16_t vsa, uint16_t bfa);
hal_status_t HAL_Display_Mock_SetScrollStart(uint16_t vss);

int HAL_Display_Mock_GetWidth(void);
int HAL_Display_Mock_GetHeight(void);

extern const display_backend_ops_t HAL_DISPLAY_MOCK_OPS;

#ifdef __cplusplus
}
#endif

#endif /* CHRONOLINK_HAL_DISPLAY_MOCK_H */