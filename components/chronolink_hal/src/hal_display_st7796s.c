#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_display_st7796s.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

static spi_device_handle_t st7796s_spi;

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
