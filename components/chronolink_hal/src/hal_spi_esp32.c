#include "hal_spi.h"
#include "pdl_pins.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "HAL_SPI";

#ifndef HAL_SPI_HOST
#define HAL_SPI_HOST SPI2_HOST
#endif

#ifndef HAL_SPI_MAX_DEVICES
#define HAL_SPI_MAX_DEVICES 4
#endif

#ifndef HAL_SPI_DEFAULT_SPEED_HZ
#define HAL_SPI_DEFAULT_SPEED_HZ 1000000
#endif

#ifndef HAL_SPI_MAX_TRANSFER_SZ
#define HAL_SPI_MAX_TRANSFER_SZ 4096
#endif

/* --------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------- */
typedef struct {
    uint8_t             cs_pin;
    spi_device_handle_t handle;
    bool                in_use;
} spi_dev_slot_t;

static bool           s_spi_bus_inited = false;
static spi_dev_slot_t s_dev_slots[HAL_SPI_MAX_DEVICES];

/* --------------------------------------------------------------------------
 * Bus init (idempotent)
 * -------------------------------------------------------------------------- */
static hal_status_t hal_spi_bus_init(void)
{
    if (s_spi_bus_inited) {
        return HAL_OK;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num     = PDL_PIN_SPI_MOSI,
        .miso_io_num     = PDL_PIN_SPI_MISO,
        .sclk_io_num     = PDL_PIN_SPI_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = HAL_SPI_MAX_TRANSFER_SZ,
    };

    esp_err_t err = spi_bus_initialize(HAL_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        s_spi_bus_inited = true;
        ESP_LOGI(TAG, "SPI bus initialized on host %d", HAL_SPI_HOST);
        return HAL_OK;
    }

    if (err == ESP_ERR_INVALID_STATE) {
        /* Another driver (e.g., ST7796S) already initialized this bus.
           We treat this as success and share the bus. */
        s_spi_bus_inited = true;
        ESP_LOGW(TAG, "SPI bus already initialized (shared with another driver)");
        return HAL_OK;
    }

    ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
    return HAL_ERR_INIT;
}

/* --------------------------------------------------------------------------
 * Public init
 * -------------------------------------------------------------------------- */
hal_status_t HAL_SPI_Init(void)
{
    return hal_spi_bus_init();
}

/* --------------------------------------------------------------------------
 * Device slot helpers
 * -------------------------------------------------------------------------- */
static int find_slot_by_cs(uint8_t cs_pin)
{
    for (int i = 0; i < HAL_SPI_MAX_DEVICES; i++) {
        if (s_dev_slots[i].in_use && s_dev_slots[i].cs_pin == cs_pin) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void)
{
    for (int i = 0; i < HAL_SPI_MAX_DEVICES; i++) {
        if (!s_dev_slots[i].in_use) {
            return i;
        }
    }
    return -1;
}

static hal_status_t hal_spi_ensure_device(uint8_t cs_pin, uint32_t speed_hz,
                                          spi_device_handle_t *out_dev)
{
    int idx = find_slot_by_cs(cs_pin);
    if (idx >= 0) {
        *out_dev = s_dev_slots[idx].handle;
        return HAL_OK;
    }

    idx = find_free_slot();
    if (idx < 0) {
        ESP_LOGE(TAG, "No free SPI device slots (max=%d)", HAL_SPI_MAX_DEVICES);
        return HAL_ERR_INIT;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = speed_hz ? speed_hz : HAL_SPI_DEFAULT_SPEED_HZ,
        .mode           = 0,
        .spics_io_num   = cs_pin,
        .queue_size     = 1,
        .pre_cb         = NULL,
        .post_cb        = NULL,
    };

    esp_err_t err = spi_bus_add_device(HAL_SPI_HOST, &devcfg, &s_dev_slots[idx].handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device (CS=%d) failed: %s", cs_pin, esp_err_to_name(err));
        return HAL_ERR_INIT;
    }

    s_dev_slots[idx].cs_pin = cs_pin;
    s_dev_slots[idx].in_use = true;
    *out_dev = s_dev_slots[idx].handle;

    ESP_LOGI(TAG, "SPI device registered: CS=%d speed=%lu Hz", cs_pin,
             (unsigned long)(speed_hz ? speed_hz : HAL_SPI_DEFAULT_SPEED_HZ));
    return HAL_OK;
}

/* --------------------------------------------------------------------------
 * Transfer
 * -------------------------------------------------------------------------- */
hal_status_t HAL_SPI_Transfer(uint8_t cs_pin,
                              const uint8_t *tx,
                              uint8_t       *rx,
                              size_t         len,
                              uint32_t       speed_hz)
{
    if (len == 0) {
        return HAL_OK;
    }

    hal_status_t hs = hal_spi_bus_init();
    if (hs != HAL_OK) {
        return hs;
    }

    spi_device_handle_t dev;
    hs = hal_spi_ensure_device(cs_pin, speed_hz, &dev);
    if (hs != HAL_OK) {
        return hs;
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length    = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    esp_err_t err = spi_device_polling_transmit(dev, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transfer failed (CS=%d len=%u): %s",
                 cs_pin, (unsigned)len, esp_err_to_name(err));
        return HAL_ERR_DEV;
    }

    return HAL_OK;
}