#include "hal_spi.h"
#include "pdl_pins.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "HAL_SPI";
static bool spi_bus_ready = false;

/* Default SPI host and DMA channel used across HAL */
#ifndef HAL_SPI_HOST
#define HAL_SPI_HOST SPI2_HOST
#endif
#ifndef HAL_SPI_DMA_CH
#define HAL_SPI_DMA_CH SPI_DMA_CH_AUTO
#endif

hal_status_t HAL_SPI_Init(void)
{
    if (spi_bus_ready) {
        return HAL_OK;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = PDL_PIN_SPI_MOSI,
        .miso_io_num = PDL_PIN_SPI_MISO,
        .sclk_io_num = PDL_PIN_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };

    esp_err_t err = spi_bus_initialize(HAL_SPI_HOST, &buscfg, HAL_SPI_DMA_CH);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return HAL_ERR_INIT;
    }

    spi_bus_ready = true;
    ESP_LOGI(TAG, "SPI bus initialized (MOSI=%d MISO=%d SCLK=%d)",
             PDL_PIN_SPI_MOSI, PDL_PIN_SPI_MISO, PDL_PIN_SPI_SCLK);
    return HAL_OK;
}

hal_status_t HAL_SPI_Transfer(uint8_t cs_pin, const uint8_t *tx, uint8_t *rx, size_t len, uint32_t speed_hz)
{
    if (!spi_bus_ready) {
        hal_status_t s = HAL_SPI_Init();
        if (s != HAL_OK) return s;
    }

    spi_device_handle_t dev;
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = (int)(speed_hz ? speed_hz : 10 * 1000 * 1000),
        .mode = 0,
        .spics_io_num = cs_pin,
        .queue_size = 1
    };

    esp_err_t err = spi_bus_add_device(HAL_SPI_HOST, &devcfg, &dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return HAL_ERR_INIT;
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = (int)(len * 8);
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    err = spi_device_transmit(dev, &t);
    spi_bus_remove_device(dev);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_device_transmit failed: %s", esp_err_to_name(err));
        return HAL_ERR_DEV;
    }

    return HAL_OK;
}
