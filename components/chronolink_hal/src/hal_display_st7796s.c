#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_display_st7796s.h"
#include "hal_display.h"

static spi_device_handle_t st7796s_spi;

static void st7796s_send_cmd_internal(uint8_t cmd)
{
    gpio_set_level(LCD_DC, 0);
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
    gpio_set_level(LCD_DC, 1);
    spi_transaction_t t = (spi_transaction_t){
        .length = len * 8,
        .tx_buffer = data,
        .rx_buffer = NULL,
        .flags = 0,
        .user = NULL
    };
    spi_device_transmit(st7796s_spi, &t);
}

hal_status_t HAL_Display_Init(void)
{
    // implementation...
    return HAL_OK;
}

void HAL_Display_WriteText(const char *text)
{
    // Placeholder ' later replaced with font renderer
    ESP_LOGI(TAG, "Display text: %s", text);
}

void st7796s_init(void)
{
    (void)HAL_Display_Init();
}
