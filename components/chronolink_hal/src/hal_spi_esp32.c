#include "hal_spi.h"
#include "pdl_pins.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "HAL_SPI";

/* Simple blocking transfer using a temporary device on the bus.
   For performance, you can later reuse a persistent spi_device_handle_t per device. */
hal_status_t HAL_SPI_Transfer(uint8_t cs_pin, const uint8_t *tx, uint8_t *rx, size_t len, uint32_t speed_hz)
{
    spi_device_handle_t dev;
    spi_bus_config_t buscfg = {
        .mosi_io_num = PDL_PIN_SPI_MOSI,
        .miso_io_num = PDL_PIN_SPI_MISO,
        .sclk_io_num = PDL_PIN_SPI_SCLK,
        .max_transfer_sz = (int)len
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = speed_hz,
        .mode = 0,
        .spics_io_num = cs_pin,
        .queue_size = 1
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return HAL_ERR_INIT;
    }

    err = spi_bus_add_device(SPI2_HOST, &devcfg, &dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return HAL_ERR_INIT;
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
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
