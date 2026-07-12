#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_display.h"
#include "hal_display_st7796s.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static spi_device_handle_t st7796s_spi;
static const char *TAG = "HAL_ST7796S";

void st7796s_send_command(uint8_t cmd)
{
    if (!st7796s_spi) {
        return;
    }

    gpio_set_level(BOARD_LCD_DC, 0);
    spi_transaction_t t = (spi_transaction_t){
        .length = 8,
        .tx_buffer = &cmd,
        .rx_buffer = NULL,
        .flags = 0,
        .user = NULL
    };
    spi_device_transmit(st7796s_spi, &t);
}

void st7796s_send_data(const uint8_t *data, size_t len)
{
    if (!st7796s_spi || !data || len == 0U) {
        return;
    }

    gpio_set_level(BOARD_LCD_DC, 1);
    spi_transaction_t t = (spi_transaction_t){
        .length = len * 8,
        .tx_buffer = data,
        .rx_buffer = NULL,
        .flags = 0,
        .user = NULL
    };
    spi_device_transmit(st7796s_spi, &t);
}

void st7796s_init(void)
{
    gpio_set_direction(BOARD_LCD_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(BOARD_LCD_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(BOARD_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(BOARD_LCD_BL, GPIO_MODE_OUTPUT);

    gpio_set_level(BOARD_LCD_CS, 1);
    st7796s_reset();
    gpio_set_level(BOARD_LCD_BL, 1);
}

void st7796s_reset(void)
{
    gpio_set_level(BOARD_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(BOARD_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

hal_status_t HAL_Display_ST7796S_Init(void)
{
    st7796s_init();
    // TODO: Add full ST7796S panel initialization command sequence.
    ESP_LOGI(TAG, "ST7796S backend initialized");
    return HAL_OK;
}

hal_status_t HAL_Display_ST7796S_WriteText(const char *text)
{
    (void)text;
    // TODO: Integrate glyph rendering and text drawing pipeline.
    return HAL_OK;
}

hal_status_t HAL_Display_ST7796S_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    (void)x;
    (void)y;
    (void)color;
    // TODO: Implement ST7796S address-window setup and single-pixel write.
    return HAL_OK;
}

hal_status_t HAL_Display_ST7796S_Clear(void)
{
    return HAL_Display_ST7796S_Fill(0U);
}

hal_status_t HAL_Display_ST7796S_Fill(uint32_t color)
{
    (void)color;
    // TODO: Implement full-frame fill path for ST7796S.
    return HAL_OK;
}

hal_status_t HAL_Display_ST7796S_Show(void)
{
    // TODO: Add explicit flush if a framebuffer is introduced.
    return HAL_OK;
}
