#include "hal_display.h"
#include "hal.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include "hal_display.h"
#include "driver/spi_master.h"
#include "esp_log.h"

#define TAG "HAL_DISPLAY"

// SPI handle
static spi_device_handle_t st7796s_spi;

// ST7796S commands
#define ST7796S_CMD_SWRESET   0x01
#define ST7796S_CMD_SLPOUT    0x11
#define ST7796S_CMD_DISPON    0x29
#define ST7796S_CMD_CASET     0x2A
#define ST7796S_CMD_RASET     0x2B
#define ST7796S_CMD_RAMWR     0x2C

// GPIO pins (example â€” adjust to your PCB)
#define LCD_CS   5
#define LCD_DC   6
#define LCD_RST  7

static void st7796s_send_cmd(uint8_t cmd)
{
    gpio_set_level(LCD_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd
    };
    spi_device_transmit(st7796s_spi, &t);
}

static void st7796s_send_data(const uint8_t *data, int len)
{
    gpio_set_level(LCD_DC, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data
    };
    spi_device_transmit(st7796s_spi, &t);
}

hal_status_t HAL_Display_Init(void)
{
    ESP_LOGI(TAG, "Initializing ST7796S display");

    // Configure GPIO
    gpio_set_direction(LCD_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(LCD_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(LCD_RST, GPIO_MODE_OUTPUT);

    // Reset sequence
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // SPI config
    spi_bus_config_t buscfg = {
        .mosi_io_num = 11,
        .miso_io_num = -1,
        .sclk_io_num = 10,
        .max_transfer_sz = 320 * 480 * 2
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = LCD_CS,
        .queue_size = 7
    };

    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI2_HOST, &devcfg, &st7796s_spi);

    // ST7796S init sequence
    st7796s_send_cmd(ST7796S_CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    st7796s_send_cmd(ST7796S_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(150));

    st7796s_send_cmd(ST7796S_CMD_DISPON);

    ESP_LOGI(TAG, "ST7796S display initialized");
    return HAL_OK;
}

void HAL_Display_WriteText(const char *text)
{
    // Placeholder â€” later replaced with font renderer
    ESP_LOGI(TAG, "Display text: %s", text);
}
