#include "sdkconfig.h"
#include "hal_display.h"
#include "esp_log.h"

static const char *TAG = "HAL_DISPLAY";

hal_status_t HAL_Display_Init(void)
{
#if CONFIG_BOARD_DISPLAY_ST7796S
    ESP_LOGI(TAG, "Initializing ST7796S display");
    return HAL_Display_ST7796S_Init();

#elif CONFIG_BOARD_DISPLAY_MAX7219
    ESP_LOGI(TAG, "Initializing MAX7219 display");
    return HAL_Display_MAX7219_Init();

#elif CONFIG_BOARD_DISPLAY_REMOTE
    ESP_LOGI(TAG, "Initializing Remote Display backend");
    return HAL_Display_Remote_Init();

#else
    ESP_LOGI(TAG, "No display selected");
    return HAL_OK;
#endif
}

hal_status_t HAL_Display_WriteText(const char *text)
{
#if CONFIG_BOARD_DISPLAY_ST7796S
    return HAL_Display_ST7796S_WriteText(text);

#elif CONFIG_BOARD_DISPLAY_MAX7219
    return HAL_Display_MAX7219_WriteText(text);

#elif CONFIG_BOARD_DISPLAY_REMOTE
    return HAL_Display_Remote_WriteText(text);

#elif CONFIG_BOARD_DISPLAY_NONE
    (void)text;
    return HAL_OK;

#else
    (void)text;
    return HAL_ERR_DEV;
#endif
}

hal_status_t HAL_Display_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
#if CONFIG_BOARD_DISPLAY_ST7796S
    return HAL_Display_ST7796S_DrawPixel(x, y, color);

#elif CONFIG_BOARD_DISPLAY_MAX7219
    return HAL_Display_MAX7219_DrawPixel(x, y, color);

#elif CONFIG_BOARD_DISPLAY_REMOTE
    return HAL_Display_Remote_DrawPixel(x, y, color);

#elif CONFIG_BOARD_DISPLAY_NONE
    (void)x;
    (void)y;
    (void)color;
    return HAL_OK;

#else
    (void)x;
    (void)y;
    (void)color;
    return HAL_ERR_DEV;
#endif
}

hal_status_t HAL_Display_Clear(void)
{
#if CONFIG_BOARD_DISPLAY_ST7796S
    return HAL_Display_ST7796S_Clear();

#elif CONFIG_BOARD_DISPLAY_MAX7219
    return HAL_Display_MAX7219_Clear();

#elif CONFIG_BOARD_DISPLAY_REMOTE
    return HAL_Display_Remote_Clear();

#elif CONFIG_BOARD_DISPLAY_NONE
    return HAL_OK;

#else
    return HAL_ERR_DEV;
#endif
}

hal_status_t HAL_Display_Fill(uint32_t color)
{
#if CONFIG_BOARD_DISPLAY_ST7796S
    return HAL_Display_ST7796S_Fill(color);

#elif CONFIG_BOARD_DISPLAY_MAX7219
    return HAL_Display_MAX7219_Fill(color);

#elif CONFIG_BOARD_DISPLAY_REMOTE
    return HAL_Display_Remote_Fill(color);

#elif CONFIG_BOARD_DISPLAY_NONE
    (void)color;
    return HAL_OK;

#else
    (void)color;
    return HAL_ERR_DEV;
#endif
}

hal_status_t HAL_Display_Show(void)
{
#if CONFIG_BOARD_DISPLAY_ST7796S
    return HAL_Display_ST7796S_Show();

#elif CONFIG_BOARD_DISPLAY_MAX7219
    return HAL_Display_MAX7219_Show();

#elif CONFIG_BOARD_DISPLAY_REMOTE
    return HAL_Display_Remote_Show();

#elif CONFIG_BOARD_DISPLAY_NONE
    return HAL_OK;

#else
    return HAL_ERR_DEV;
#endif
}
