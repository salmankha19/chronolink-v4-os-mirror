#ifndef CHRONOLINK_HAL_DISPLAY_REMOTE_H
#define CHRONOLINK_HAL_DISPLAY_REMOTE_H

#include "hal_display.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public HDL API for Remote backend */
hal_status_t HAL_Display_Remote_Init(void);
hal_status_t HAL_Display_Remote_WriteText(const char *text);
hal_status_t HAL_Display_Remote_DrawPixel(uint16_t x, uint16_t y, uint32_t color);
hal_status_t HAL_Display_Remote_Clear(void);
hal_status_t HAL_Display_Remote_Fill(uint32_t color);
hal_status_t HAL_Display_Remote_Show(void);

/* Capability query */
hal_status_t HAL_Display_Remote_HasCapability(hal_display_cap_t cap);

hal_status_t HAL_Display_Remote_Deinit(void);
hal_status_t HAL_Display_Remote_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
hal_status_t HAL_Display_Remote_BlitRow(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len);

/* Panel dimensions (unknown until remote handshake) */
int HAL_Display_Remote_GetWidth(void);
int HAL_Display_Remote_GetHeight(void);

/* Ops table exported to the router */
extern const display_backend_ops_t HAL_DISPLAY_REMOTE_OPS;

#ifdef __cplusplus
}
#endif

#endif /* CHRONOLINK_HAL_DISPLAY_REMOTE_H */