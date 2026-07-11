#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_display_st7796s.h"
#include "hal_display.h"
#include "hal.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include <string.h>
#include "pdl_pins.h"

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

// Use PDL pins instead of hardcoded values
#define LCD_CS   PDL_PIN_SPI_CS_DISPLAY
#define LCD_DC   PDL_PIN_DISPLAY_DC
#define LCD_RST  PDL_PIN_DISPLAY_RST
#define LCD_BL   PDL_PIN_DISPLAY_BL

static void st7796s_send_cmd_internal(uint8_t cmd)
{
    gpio_set_level(LCD_DC, 0);
    spi_transaction_t t = (spi_transaction_t){
    .length = 8 * bytes,
    .tx_buffer = data,
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
    .length = 8 * bytes,
    .tx_buffer = data,
    .rx_buffer = NULL,
    .flags = 0,
    .user = NULL
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
#ifdef LCD_BL
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 1);
#endif

    // Reset sequence
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // SPI config using PDL SPI pins
    spi_bus_config_t buscfg = {
        .mosi_io_num = PDL_PIN_SPI_MOSI,
        .miso_io_num = PDL_PIN_SPI_MISO,
        .sclk_io_num = PDL_PIN_SPI_SCLK,
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
    st7796s_send_cmd_internal(ST7796S_CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    st7796s_send_cmd_internal(ST7796S_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(150));

    st7796s_send_cmd_internal(ST7796S_CMD_DISPON);

    ESP_LOGI(TAG, "ST7796S display initialized");
    return HAL_OK;
}

void HAL_Display_WriteText(const char *text)
{
    // Placeholder ï¿½ later replaced with font renderer
    ESP_LOGI(TAG, "Display text: %s", text);
}

void st7796s_send_command(uint8_t cmd)
{
    st7796s_send_cmd_internal(cmd);
}

void st7796s_reset(void)
{
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

void st7796s_init(void)
{
    (void)HAL_Display_Init();
}

