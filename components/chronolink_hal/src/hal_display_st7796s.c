/*
 * hal_display_st7796s.c
 *
 * Robust ST7796S 320x480 TFT LCD driver for ChronoLink V4 OS
 *
 * Key safety and performance features
 *  - DMA-safe SPI transfers (heap_caps_malloc with MALLOC_CAP_DMA)
 *  - No stack pointers passed to DMA
 *  - SPI driver manages CS (spics_io_num)
 *  - Chunked transfers for fills and clears
 *  - Correct RGB24 -> RGB565 conversion
 *  - Init sequence checks and ID read option
 *  - Deinit turns display off and backlight off
 *
 * Ensure board_pins.h defines BOARD_LCD_CS, BOARD_LCD_DC, BOARD_LCD_RST,
 * BOARD_LCD_BL, PDL_PIN_SPI_MOSI, PDL_PIN_SPI_MISO, PDL_PIN_SPI_SCLK as GPIO numbers.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal.h"
#include "hal_display.h"
#include "hal_display_st7796s.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static const char *TAG = "HAL_ST7796S";

/* --------------------------------------------------------------------------
 * Configurable defaults (override in project headers or Kconfig)
 * -------------------------------------------------------------------------- */
#ifndef ST7796S_SPI_HOST
#define ST7796S_SPI_HOST SPI2_HOST
#endif

#ifndef ST7796S_SPI_CLOCK_HZ
#define ST7796S_SPI_CLOCK_HZ (20 * 1000 * 1000) /* 20 MHz default */
#endif

#ifndef ST7796S_DMA_CHUNK_BYTES
#define ST7796S_DMA_CHUNK_BYTES (8 * 1024) /* 8 KB chunk */
#endif

#define FILL_CHUNK_PIXELS (ST7796S_DMA_CHUNK_BYTES / 2)

#ifndef ST7796S_WIDTH
#define ST7796S_WIDTH 320
#endif

#ifndef ST7796S_HEIGHT
#define ST7796S_HEIGHT 480
#endif

#define ST7796S_COLMOD_RGB565 0x55
#define ST7796S_DEFAULT_MADCTL (0x40 | 0x08) /* MX | BGR */

/* --------------------------------------------------------------------------
 * ST7796S commands used
 * -------------------------------------------------------------------------- */
#define ST7796S_SWRESET   0x01
#define ST7796S_SLPOUT    0x11
#define ST7796S_NORON     0x13
#define ST7796S_DISPON    0x29
#define ST7796S_CASET     0x2A
#define ST7796S_RASET     0x2B
#define ST7796S_RAMWR     0x2C
#define ST7796S_MADCTL    0x36
#define ST7796S_COLMOD    0x3A
#define ST7796S_INVON     0x21
#define ST7796S_FRMCTR1   0xB1
#define ST7796S_INVCTR    0xB4
#define ST7796S_PWCTR1    0xC0
#define ST7796S_PWCTR2    0xC1
#define ST7796S_PWCTR3    0xC2
#define ST7796S_VMCTR1    0xC5
#define ST7796S_VMCTR2    0xC6
#define ST7796S_GMCTRP1   0xE0
#define ST7796S_GMCTRN1   0xE1
#define ST7796S_RDDID     0x04

/* --------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------- */
static spi_device_handle_t st7796s_spi = NULL;
static bool st7796s_initialized = false;
static uint16_t st7796s_win_x1 = 0;
static uint16_t st7796s_win_y1 = 0;
static uint16_t st7796s_win_x2 = ST7796S_WIDTH - 1;
static uint16_t st7796s_win_y2 = ST7796S_HEIGHT - 1;

/* Dedicated reusable DMA buffer used only for fills/clears.
   It is never reused for arbitrary transfers to avoid races. */
static uint8_t *st7796s_fill_dma_buf = NULL;
static size_t st7796s_fill_dma_bytes = 0;

/* --------------------------------------------------------------------------
 * Static const gamma tables to keep in flash
 * -------------------------------------------------------------------------- */
static const uint8_t gamma_pos[14] = {
    0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F,
    0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B
};

static const uint8_t gamma_neg[14] = {
    0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B,
    0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B
};

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */
static hal_status_t map_esp_err(esp_err_t e)
{
    if (e == ESP_OK) return HAL_OK;
    if (e == ESP_ERR_NO_MEM) return HAL_ERR_INIT;
    if (e == ESP_ERR_INVALID_STATE) return HAL_ERR_INIT;
    if (e == ESP_ERR_INVALID_ARG) return HAL_ERR_DEV;
    if (e == ESP_ERR_TIMEOUT) return HAL_ERR_BUS;
    return HAL_ERR_DEV;
}

static inline uint16_t rgb24_to_rgb565(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Validate GPIO macros at compile time where possible */
#if defined(BOARD_LCD_DC) && (BOARD_LCD_DC < 0 || BOARD_LCD_DC > 63)
#error "BOARD_LCD_DC out of valid GPIO range"
#endif

/* Runtime-safe setter for DC pin */
static inline void st7796s_dc_set(int level)
{
    if ((int)BOARD_LCD_DC >= 0 && (int)BOARD_LCD_DC <= 63) {
        gpio_set_level((gpio_num_t)BOARD_LCD_DC, level);
    }
}

/* Ensure fill DMA buffer exists and is large enough */
static bool ensure_fill_dma_buf(size_t bytes)
{
    if (st7796s_fill_dma_bytes >= bytes && st7796s_fill_dma_buf) return true;
    if (st7796s_fill_dma_buf) {
        heap_caps_free(st7796s_fill_dma_buf);
        st7796s_fill_dma_buf = NULL;
        st7796s_fill_dma_bytes = 0;
    }
    st7796s_fill_dma_buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
    if (!st7796s_fill_dma_buf) {
        ESP_LOGE(TAG, "Failed to allocate fill DMA buffer (%u bytes)", (unsigned)bytes);
        return false;
    }
    st7796s_fill_dma_bytes = bytes;
    return true;
}

/* --------------------------------------------------------------------------
 * DMA-safe SPI helpers
 * - small transfers use SPI_TRANS_USE_TXDATA
 * - larger transfers allocate a temporary DMA buffer (not reusing fill buffer)
 * -------------------------------------------------------------------------- */

static esp_err_t st7796s_send_command_raw(uint8_t cmd)
{
    if (!st7796s_spi) return ESP_ERR_INVALID_STATE;
    st7796s_dc_set(0);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = cmd;
    return spi_device_polling_transmit(st7796s_spi, &t);
}

static esp_err_t st7796s_send_data_raw(const uint8_t *data, size_t len)
{
    if (!st7796s_spi) return ESP_ERR_INVALID_STATE;
    if (!data || len == 0) return ESP_OK;

    st7796s_dc_set(1);

    if (len <= 4) {
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = len * 8;
        t.flags = SPI_TRANS_USE_TXDATA;
        memcpy(t.tx_data, data, len);
        return spi_device_polling_transmit(st7796s_spi, &t);
    }

    /* For larger transfers allocate a temporary DMA buffer to avoid races */
    uint8_t *dma_buf = heap_caps_malloc(len, MALLOC_CAP_DMA);
    if (!dma_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer for %u bytes", (unsigned)len);
        return ESP_ERR_NO_MEM;
    }
    memcpy(dma_buf, data, len);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = dma_buf;
    esp_err_t err = spi_device_polling_transmit(st7796s_spi, &t);
    heap_caps_free(dma_buf);
    return err;
}

/* Helper to send command + single byte */
static esp_err_t st7796s_send_cmd_with_byte(uint8_t cmd, uint8_t b)
{
    esp_err_t e = st7796s_send_command_raw(cmd);
    if (e != ESP_OK) return e;
    return st7796s_send_data_raw(&b, 1);
}

/* Helper to send command + buffer */
static esp_err_t st7796s_send_cmd_with_data(uint8_t cmd, const uint8_t *buf, size_t len)
{
    esp_err_t e = st7796s_send_command_raw(cmd);
    if (e != ESP_OK) return e;
    return st7796s_send_data_raw(buf, len);
}

/* --------------------------------------------------------------------------
 * GPIO init and reset
 * -------------------------------------------------------------------------- */
static void st7796s_gpio_init(void)
{
    /* Build pin mask for DC, RST, BL if valid */
    uint64_t mask = 0;
    if ((int)BOARD_LCD_DC >= 0 && (int)BOARD_LCD_DC <= 63) mask |= (1ULL << BOARD_LCD_DC);
    if ((int)BOARD_LCD_RST >= 0 && (int)BOARD_LCD_RST <= 63) mask |= (1ULL << BOARD_LCD_RST);
    if ((int)BOARD_LCD_BL >= 0 && (int)BOARD_LCD_BL <= 63) mask |= (1ULL << BOARD_LCD_BL);

    if (mask == 0) {
        ESP_LOGW(TAG, "No control pins defined for ST7796S");
        return;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    /* Default states */
    st7796s_dc_set(0);
    if ((int)BOARD_LCD_RST >= 0 && (int)BOARD_LCD_RST <= 63) gpio_set_level((gpio_num_t)BOARD_LCD_RST, 1);
    if ((int)BOARD_LCD_BL >= 0 && (int)BOARD_LCD_BL <= 63) gpio_set_level((gpio_num_t)BOARD_LCD_BL, 0);
}

static void st7796s_hard_reset(void)
{
    if ((int)BOARD_LCD_RST < 0 || (int)BOARD_LCD_RST > 63) {
        ESP_LOGW(TAG, "RST pin not defined, skipping hardware reset");
        return;
    }
    gpio_set_level((gpio_num_t)BOARD_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)BOARD_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/* --------------------------------------------------------------------------
 * SPI init
 * -------------------------------------------------------------------------- */
static hal_status_t st7796s_spi_init(void)
{
    if (st7796s_spi) return HAL_OK;

    spi_bus_config_t buscfg = {
        .mosi_io_num = PDL_PIN_SPI_MOSI,
        .miso_io_num = PDL_PIN_SPI_MISO,
        .sclk_io_num = PDL_PIN_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7796S_DMA_CHUNK_BYTES,
        .flags = 0,
        .intr_flags = 0
    };

    esp_err_t err = spi_bus_initialize(ST7796S_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return HAL_ERR_INIT;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = ST7796S_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = BOARD_LCD_CS,
        .queue_size = 3,
        .pre_cb = NULL,
        .post_cb = NULL,
        .flags = 0
    };

    err = spi_bus_add_device(ST7796S_SPI_HOST, &devcfg, &st7796s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return HAL_ERR_INIT;
    }

    ESP_LOGI(TAG, "SPI initialized host=%d clk=%u", ST7796S_SPI_HOST, ST7796S_SPI_CLOCK_HZ);
    return HAL_OK;
}

/* --------------------------------------------------------------------------
 * Address window and memory write
 * -------------------------------------------------------------------------- */
static void st7796s_set_address_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t colbuf[4] = {
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF),
        (uint8_t)(x2 >> 8), (uint8_t)(x2 & 0xFF)
    };
    st7796s_send_cmd_with_data(ST7796S_CASET, colbuf, sizeof(colbuf));

    uint8_t rowbuf[4] = {
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF),
        (uint8_t)(y2 >> 8), (uint8_t)(y2 & 0xFF)
    };
    st7796s_send_cmd_with_data(ST7796S_RASET, rowbuf, sizeof(rowbuf));

    st7796s_win_x1 = x1;
    st7796s_win_y1 = y1;
    st7796s_win_x2 = x2;
    st7796s_win_y2 = y2;
}

static void st7796s_write_memory_prepare(void)
{
    st7796s_send_command_raw(ST7796S_RAMWR);
}

/* --------------------------------------------------------------------------
 * Optional ID read (useful for bring-up). Returns ESP_OK on success.
 * Requires MISO wired and device supporting read.
 * -------------------------------------------------------------------------- */
static esp_err_t st7796s_read_id(uint8_t *out, size_t out_len)
{
    if (!st7796s_spi || !out || out_len == 0) return ESP_ERR_INVALID_ARG;

    /* Send RDDID command then read bytes. Use a transaction with rx_buffer. */
    st7796s_send_command_raw(ST7796S_RDDID);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = out_len * 8;
    t.rxlength = out_len * 8;
    t.rx_buffer = out;
    /* For reads, tx_buffer can be NULL; some panels require dummy bytes. */
    return spi_device_polling_transmit(st7796s_spi, &t);
}

/* --------------------------------------------------------------------------
 * Initialization sequence with error checks
 * -------------------------------------------------------------------------- */
static hal_status_t st7796s_run_init_sequence(void)
{
    ESP_LOGI(TAG, "Running ST7796S init sequence");

    esp_err_t e;

    e = st7796s_send_command_raw(ST7796S_SWRESET);
    if (e != ESP_OK) { ESP_LOGE(TAG, "SWRESET failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    vTaskDelay(pdMS_TO_TICKS(10));

    e = st7796s_send_command_raw(ST7796S_SLPOUT);
    if (e != ESP_OK) { ESP_LOGE(TAG, "SLPOUT failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    vTaskDelay(pdMS_TO_TICKS(120));

    /* MADCTL */
    e = st7796s_send_command_raw(ST7796S_MADCTL);
    if (e != ESP_OK) { ESP_LOGE(TAG, "MADCTL cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    uint8_t madctl = (uint8_t)ST7796S_DEFAULT_MADCTL;
    e = st7796s_send_data_raw(&madctl, 1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "MADCTL data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    /* COLMOD */
    e = st7796s_send_command_raw(ST7796S_COLMOD);
    if (e != ESP_OK) { ESP_LOGE(TAG, "COLMOD cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    uint8_t colmod = ST7796S_COLMOD_RGB565;
    e = st7796s_send_data_raw(&colmod, 1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "COLMOD data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    /* INVON */
    e = st7796s_send_command_raw(ST7796S_INVON);
    if (e != ESP_OK) { ESP_LOGW(TAG, "INVON failed: %s", esp_err_to_name(e)); /* non-fatal */ }

    /* FRMCTR1 */
    e = st7796s_send_command_raw(ST7796S_FRMCTR1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "FRMCTR1 cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    uint8_t frmctr1[] = {0x00, 0x1B};
    e = st7796s_send_data_raw(frmctr1, sizeof(frmctr1));
    if (e != ESP_OK) { ESP_LOGE(TAG, "FRMCTR1 data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    /* INVCTR */
    e = st7796s_send_command_raw(ST7796S_INVCTR);
    if (e != ESP_OK) { ESP_LOGE(TAG, "INVCTR cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    uint8_t invctr = 0x01;
    e = st7796s_send_data_raw(&invctr, 1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "INVCTR data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    /* Power controls */
    e = st7796s_send_command_raw(ST7796S_PWCTR1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "PWCTR1 cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    uint8_t pw1[] = {0x17, 0x15, 0x00};
    e = st7796s_send_data_raw(pw1, sizeof(pw1));
    if (e != ESP_OK) { ESP_LOGE(TAG, "PWCTR1 data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    e = st7796s_send_command_raw(ST7796S_PWCTR2);
    if (e != ESP_OK) { ESP_LOGE(TAG, "PWCTR2 cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    uint8_t pw2 = 0x41;
    e = st7796s_send_data_raw(&pw2, 1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "PWCTR2 data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    e = st7796s_send_command_raw(ST7796S_PWCTR3);
    if (e != ESP_OK) { ESP_LOGE(TAG, "PWCTR3 cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    uint8_t pw3[] = {0x00, 0x12, 0x12};
    e = st7796s_send_data_raw(pw3, sizeof(pw3));
    if (e != ESP_OK) { ESP_LOGE(TAG, "PWCTR3 data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    e = st7796s_send_command_raw(ST7796S_VMCTR1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "VMCTR1 cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    uint8_t vm1[] = {0x00, 0x3C, 0x00};
    e = st7796s_send_data_raw(vm1, sizeof(vm1));
    if (e != ESP_OK) { ESP_LOGE(TAG, "VMCTR1 data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    e = st7796s_send_command_raw(ST7796S_VMCTR2);
    if (e != ESP_OK) { ESP_LOGE(TAG, "VMCTR2 cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    uint8_t vm2 = 0x00;
    e = st7796s_send_data_raw(&vm2, 1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "VMCTR2 data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    /* Gamma tables */
    e = st7796s_send_command_raw(ST7796S_GMCTRP1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "GMCTRP1 cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    e = st7796s_send_data_raw(gamma_pos, sizeof(gamma_pos));
    if (e != ESP_OK) { ESP_LOGE(TAG, "GMCTRP1 data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    e = st7796s_send_command_raw(ST7796S_GMCTRN1);
    if (e != ESP_OK) { ESP_LOGE(TAG, "GMCTRN1 cmd failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    e = st7796s_send_data_raw(gamma_neg, sizeof(gamma_neg));
    if (e != ESP_OK) { ESP_LOGE(TAG, "GMCTRN1 data failed: %s", esp_err_to_name(e)); return map_esp_err(e); }

    /* Set full window */
    st7796s_set_address_window(0, 0, ST7796S_WIDTH - 1, ST7796S_HEIGHT - 1);

    /* Clear screen to black using chunked DMA-safe transfers */
    st7796s_write_memory_prepare();

    size_t chunk_pixels = FILL_CHUNK_PIXELS;
    size_t chunk_bytes = chunk_pixels * 2;
    if (!ensure_fill_dma_buf(chunk_bytes)) {
        ESP_LOGW(TAG, "Unable to allocate fill buffer; skipping clear");
    } else {
        /* Fill buffer with black */
        for (size_t i = 0; i < chunk_pixels; ++i) {
            st7796s_fill_dma_buf[2 * i] = 0x00;
            st7796s_fill_dma_buf[2 * i + 1] = 0x00;
        }

        int total_pixels = ST7796S_WIDTH * ST7796S_HEIGHT;
        int remaining = total_pixels;
        int chunks_sent = 0;
        while (remaining > 0) {
            int to_send = (remaining > (int)chunk_pixels) ? (int)chunk_pixels : remaining;
            esp_err_t er = st7796s_send_data_raw(st7796s_fill_dma_buf, to_send * 2);
            if (er != ESP_OK) {
                ESP_LOGW(TAG, "Clear chunk transmit failed: %s", esp_err_to_name(er));
                break;
            }
            remaining -= to_send;
            ++chunks_sent;
            if ((chunks_sent & 0x7) == 0) taskYIELD(); /* yield every 8 chunks */
        }
    }

    e = st7796s_send_command_raw(ST7796S_NORON);
    if (e != ESP_OK) { ESP_LOGE(TAG, "NORON failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    vTaskDelay(pdMS_TO_TICKS(10));

    e = st7796s_send_command_raw(ST7796S_DISPON);
    if (e != ESP_OK) { ESP_LOGE(TAG, "DISPON failed: %s", esp_err_to_name(e)); return map_esp_err(e); }
    vTaskDelay(pdMS_TO_TICKS(20));

    return HAL_OK;
}

/* --------------------------------------------------------------------------
 * HIL API
 * -------------------------------------------------------------------------- */
hal_status_t HAL_Display_ST7796S_Init(void)
{
    ESP_LOGI(TAG, "ST7796S backend init start");

    st7796s_gpio_init();
    st7796s_hard_reset();

    hal_status_t hs = st7796s_spi_init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "SPI init failed");
        return hs;
    }

    /* Optional ID read for bring-up (uncomment to use) */
    /*
    uint8_t idbuf[4] = {0};
    if (st7796s_read_id(idbuf, sizeof(idbuf)) == ESP_OK) {
        ESP_LOGI(TAG, "Panel ID: %02X %02X %02X %02X", idbuf[0], idbuf[1], idbuf[2], idbuf[3]);
    } else {
        ESP_LOGW(TAG, "Panel ID read failed (MISO may be unconnected)");
    }
    */

    if (st7796s_run_init_sequence() != HAL_OK) {
        ESP_LOGE(TAG, "Panel init sequence failed");
        return HAL_ERR_INIT;
    }

    /* Turn on backlight if pin defined */
    if ((int)BOARD_LCD_BL >= 0 && (int)BOARD_LCD_BL <= 63) {
        gpio_set_level((gpio_num_t)BOARD_LCD_BL, 1);
    }

    st7796s_initialized = true;
    ESP_LOGI(TAG, "ST7796S backend initialized");
    return HAL_OK;
}

hal_status_t HAL_Display_ST7796S_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    if (!st7796s_initialized) return HAL_ERR_INIT;
    if (x >= ST7796S_WIDTH || y >= ST7796S_HEIGHT) return HAL_ERR_DEV;

    uint16_t rgb565 = rgb24_to_rgb565(color);
    uint8_t pix[2] = { (uint8_t)(rgb565 >> 8), (uint8_t)(rgb565 & 0xFF) };

    st7796s_set_address_window(x, y, x, y);
    st7796s_write_memory_prepare();
    esp_err_t e = st7796s_send_data_raw(pix, 2);
    return map_esp_err(e);
}

hal_status_t HAL_Display_ST7796S_Clear(void)
{
    return HAL_Display_ST7796S_Fill(0x000000);
}

hal_status_t HAL_Display_ST7796S_Fill(uint32_t color)
{
    if (!st7796s_initialized) return HAL_ERR_INIT;

    uint16_t rgb565 = rgb24_to_rgb565(color);

    size_t chunk_pixels = FILL_CHUNK_PIXELS;
    size_t chunk_bytes = chunk_pixels * 2;
    if (!ensure_fill_dma_buf(chunk_bytes)) return HAL_ERR_INIT;

    for (size_t i = 0; i < chunk_pixels; ++i) {
        st7796s_fill_dma_buf[2 * i] = (uint8_t)(rgb565 >> 8);
        st7796s_fill_dma_buf[2 * i + 1] = (uint8_t)(rgb565 & 0xFF);
    }

    st7796s_set_address_window(0, 0, ST7796S_WIDTH - 1, ST7796S_HEIGHT - 1);
    st7796s_write_memory_prepare();

    int total_pixels = ST7796S_WIDTH * ST7796S_HEIGHT;
    int remaining = total_pixels;
    int chunks_sent = 0;
    while (remaining > 0) {
        int to_send = (remaining > (int)chunk_pixels) ? (int)chunk_pixels : remaining;
        esp_err_t e = st7796s_send_data_raw(st7796s_fill_dma_buf, to_send * 2);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "Fill chunk transmit failed: %s", esp_err_to_name(e));
            return map_esp_err(e);
        }
        remaining -= to_send;
        ++chunks_sent;
        if ((chunks_sent & 0x7) == 0) taskYIELD();
    }

    return HAL_OK;
}

hal_status_t HAL_Display_ST7796S_Show(void)
{
    if (!st7796s_initialized) return HAL_ERR_INIT;
    return HAL_OK;
}

hal_status_t HAL_Display_ST7796S_WriteText(const char *text)
{
    (void)text;
    ESP_LOGW(TAG, "WriteText not implemented");
    return HAL_OK;
}

hal_status_t HAL_Display_ST7796S_HasCapability(hal_display_cap_t cap)
{
    switch (cap) {
    case HAL_CAP_DRAW_PIXEL:
    case HAL_CAP_FILL:
    case HAL_CAP_CLEAR:
    case HAL_CAP_SHOW:
        return HAL_OK;
    case HAL_CAP_BRIGHTNESS:
        /* Brightness via PWM not implemented yet */
        return HAL_ERR_DEV;
    default:
        return HAL_ERR_DEV;
    }
}

/* Deinit helper */
void HAL_Display_ST7796S_Deinit(void)
{
    if (st7796s_spi) {
        /* Turn display off and backlight off */
        st7796s_send_command_raw(0x28); /* DISP OFF */
        vTaskDelay(pdMS_TO_TICKS(10));
        if ((int)BOARD_LCD_BL >= 0 && (int)BOARD_LCD_BL <= 63) gpio_set_level((gpio_num_t)BOARD_LCD_BL, 0);

        spi_bus_remove_device(st7796s_spi);
        st7796s_spi = NULL;
    }

    if (st7796s_fill_dma_buf) {
        heap_caps_free(st7796s_fill_dma_buf);
        st7796s_fill_dma_buf = NULL;
        st7796s_fill_dma_bytes = 0;
    }

    st7796s_initialized = false;
    ESP_LOGI(TAG, "ST7796S deinitialized");
}