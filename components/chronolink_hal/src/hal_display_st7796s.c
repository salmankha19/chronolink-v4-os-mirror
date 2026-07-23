#include "hal_display_st7796s.h"
#include "hal_gpio_safe.h"
#include "pdl_compat.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "HAL_DISPLAY_ST7796S";

// SPI handle
static spi_device_handle_t st7796s_spi;

// ST7796S commands
#define ST7796S_CMD_SWRESET   0x01
#define ST7796S_CMD_SLPOUT    0x11
#define ST7796S_CMD_DISPON    0x29
#define ST7796S_CMD_CASET     0x2A
#define ST7796S_CMD_RASET     0x2B
#define ST7796S_CMD_RAMWR     0x2C

#define LCD_HOST SPI2_HOST
#define LCD_CS   PDL_PIN_DISPLAY_CS
#define LCD_DC   PDL_PIN_DISPLAY_DC
#define LCD_RST  PDL_PIN_DISPLAY_RST
#define LCD_CONTROL_PIN_MASK ((1ULL << LCD_CS) | (1ULL << LCD_DC) | (1ULL << LCD_RST))

_Static_assert(LCD_CS < 64, "LCD_CS must fit in a 64-bit GPIO mask");
_Static_assert(LCD_DC < 64, "LCD_DC must fit in a 64-bit GPIO mask");
_Static_assert(LCD_RST < 64, "LCD_RST must fit in a 64-bit GPIO mask");

static hal_status_t st7796s_send_cmd(uint8_t cmd)
{
    if (gpio_set_level(LCD_DC, 0) != ESP_OK) {
        return HAL_ERR_BUS;
    }

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd
    };
    return spi_device_transmit(st7796s_spi, &t) == ESP_OK ? HAL_OK : HAL_ERR_BUS;
}

static hal_status_t st7796s_send_data(const uint8_t *data, int len)
{
    if (gpio_set_level(LCD_DC, 1) != ESP_OK) {
        return HAL_ERR_BUS;
    }

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data
    };
    return spi_device_transmit(st7796s_spi, &t) == ESP_OK ? HAL_OK : HAL_ERR_BUS;
}

static void st7796s_cleanup(bool bus_initialized, bool remove_device)
{
    if (remove_device && st7796s_spi != NULL) {
        if (spi_bus_remove_device(st7796s_spi) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove ST7796S SPI device");
        }
        st7796s_spi = NULL;
    }

    if (bus_initialized && spi_bus_free(LCD_HOST) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to free SPI bus");
    }
}

hal_status_t HAL_Display_Init(void)
{
    hal_status_t status = HAL_OK;
    bool bus_initialized = false;
    bool device_added = false;

    ESP_LOGI(TAG, "Initializing ST7796S display");

    if (hal_gpio_config_outputs(LCD_CONTROL_PIN_MASK) != ESP_OK) {
        return HAL_ERR_INIT;
    }

    // Reset sequence
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // SPI config
    spi_bus_config_t buscfg = {
        .mosi_io_num = PDL_PIN_SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PDL_PIN_SPI_SCLK,
        .max_transfer_sz = 320 * 480 * 2
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = LCD_CS,
        .queue_size = 7
    };

    if (spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus");
        return HAL_ERR_BUS;
    }
    bus_initialized = true;

    if (spi_bus_add_device(LCD_HOST, &devcfg, &st7796s_spi) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ST7796S SPI device");
        status = HAL_ERR_DEV;
        goto error_cleanup;
    }
    device_added = true;

    // ST7796S init sequence
    if (st7796s_send_cmd(ST7796S_CMD_SWRESET) != HAL_OK) {
        status = HAL_ERR_BUS;
        goto error_cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    if (st7796s_send_cmd(ST7796S_CMD_SLPOUT) != HAL_OK) {
        status = HAL_ERR_BUS;
        goto error_cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    if (st7796s_send_cmd(ST7796S_CMD_DISPON) != HAL_OK) {
        status = HAL_ERR_BUS;
        goto error_cleanup;
    }

    ESP_LOGI(TAG, "ST7796S display initialized");
    return HAL_OK;

error_cleanup:
    st7796s_cleanup(bus_initialized, device_added);

    return status;
}

void HAL_Display_WriteText(const char *text)
{
    if (text == NULL) {
        return;
    }

    // Placeholder â€” later replaced with font renderer
    ESP_LOGI(TAG, "Display text: %s", text);
}
