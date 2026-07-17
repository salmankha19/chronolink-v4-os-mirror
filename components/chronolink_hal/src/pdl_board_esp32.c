#include "pdl_board.h"
#include "pdl_pins.h"
#include "pdl_capabilities.h"
#include "pdl_partitions.h"
#include "hal.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "pdl_board";

/* Helper: validate GPIO numbers at runtime to avoid invalid gpio calls */
static bool is_valid_pin(int pin)
{
    return (pin >= 0 && pin < GPIO_PIN_COUNT);
}

static bool validate_pin_or_log(const char *name, int pin)
{
    if (!is_valid_pin(pin)) {
        ESP_LOGW(TAG, "Invalid pin for %s: %d", name, pin);
        return false;
    }
    ESP_LOGI(TAG, "Pin %s = %d", name, pin);
    return true;
}

void pdl_board_config_pins(void)
{
    ESP_LOGI(TAG, "Configuring board pins");

    /* single gpio_config_t used and reinitialized per block */
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));

    /* Debug: raw macro values (temporary, remove when stable) */
#ifdef PDL_PIN_LED_STATUS
    ESP_LOGD(TAG, "DEBUG: PDL_PIN_LED_STATUS=%d", (int)PDL_PIN_LED_STATUS);
#endif
#ifdef PDL_PIN_I2C_SDA
    ESP_LOGD(TAG, "DEBUG: PDL_PIN_I2C_SDA=%d PDL_PIN_I2C_SCL=%d", (int)PDL_PIN_I2C_SDA, (int)PDL_PIN_I2C_SCL);
#endif

    /* Status LED */
#ifdef PDL_PIN_LED_STATUS
    memset(&io_conf, 0, sizeof(io_conf));
    if (validate_pin_or_log("LED_STATUS", PDL_PIN_LED_STATUS)) {
        io_conf.pin_bit_mask = (1ULL << (uint64_t)PDL_PIN_LED_STATUS);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_LOGD(TAG, "Config LED_STATUS mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
        gpio_set_level((gpio_num_t)PDL_PIN_LED_STATUS, 0);
    } else {
        ESP_LOGW(TAG, "Skipping LED_STATUS config due to invalid pin");
    }
#endif

    /* I2C pins (SDA, SCL) - leave as inputs if not used by driver init */
#if defined(PDL_PIN_I2C_SDA) && defined(PDL_PIN_I2C_SCL)
    memset(&io_conf, 0, sizeof(io_conf));
    if (validate_pin_or_log("I2C_SDA", PDL_PIN_I2C_SDA) &&
        validate_pin_or_log("I2C_SCL", PDL_PIN_I2C_SCL)) {
        io_conf.pin_bit_mask = (1ULL << (uint64_t)PDL_PIN_I2C_SDA) | (1ULL << (uint64_t)PDL_PIN_I2C_SCL);
        io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_LOGD(TAG, "Config I2C pins mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
    } else {
        ESP_LOGW(TAG, "Skipping I2C pin config due to invalid SDA/SCL pins");
    }
#endif

    /* SPI pins */
#ifdef PDL_PIN_SPI_MOSI
    memset(&io_conf, 0, sizeof(io_conf));
    if (validate_pin_or_log("SPI_MOSI", PDL_PIN_SPI_MOSI) &&
        validate_pin_or_log("SPI_MISO", PDL_PIN_SPI_MISO) &&
        validate_pin_or_log("SPI_SCLK", PDL_PIN_SPI_SCLK)) {
        io_conf.pin_bit_mask = (1ULL << (uint64_t)PDL_PIN_SPI_MOSI) |
                               (1ULL << (uint64_t)PDL_PIN_SPI_MISO) |
                               (1ULL << (uint64_t)PDL_PIN_SPI_SCLK);
        io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_LOGD(TAG, "Config SPI pins mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
    } else {
        ESP_LOGW(TAG, "Skipping SPI pin config due to invalid pins");
    }
#endif

    /* Display control pins */
#ifdef PDL_PIN_DISPLAY_DC
    memset(&io_conf, 0, sizeof(io_conf));
    if (validate_pin_or_log("DISPLAY_DC", PDL_PIN_DISPLAY_DC) &&
        validate_pin_or_log("DISPLAY_RST", PDL_PIN_DISPLAY_RST)) {
        io_conf.pin_bit_mask = (1ULL << (uint64_t)PDL_PIN_DISPLAY_DC) | (1ULL << (uint64_t)PDL_PIN_DISPLAY_RST);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_LOGD(TAG, "Config display ctrl mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
        gpio_set_level((gpio_num_t)PDL_PIN_DISPLAY_RST, 1);
    } else {
        ESP_LOGW(TAG, "Skipping display control pin config due to invalid pins");
    }
#endif

    /* Backlight */
#ifdef PDL_PIN_DISPLAY_BL
    memset(&io_conf, 0, sizeof(io_conf));
    if (validate_pin_or_log("DISPLAY_BL", PDL_PIN_DISPLAY_BL)) {
        io_conf.pin_bit_mask = (1ULL << (uint64_t)PDL_PIN_DISPLAY_BL);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_LOGD(TAG, "Config display BL mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
        gpio_set_level((gpio_num_t)PDL_PIN_DISPLAY_BL, 1);
    } else {
        ESP_LOGW(TAG, "Skipping display backlight config due to invalid pin");
    }
#endif

    /* RTC interrupt pin */
#ifdef PDL_PIN_RTC_INT
    memset(&io_conf, 0, sizeof(io_conf));
    if (validate_pin_or_log("RTC_INT", PDL_PIN_RTC_INT)) {
        io_conf.pin_bit_mask = (1ULL << (uint64_t)PDL_PIN_RTC_INT);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_NEGEDGE;
        ESP_LOGD(TAG, "Config RTC_INT mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
    } else {
        ESP_LOGW(TAG, "Skipping RTC_INT config due to invalid pin");
    }
#endif

    ESP_LOGI(TAG, "Board pins configured");
}

/* Public init called by chronolink_hal during HAL_Init */
esp_err_t pdl_board_init(void)
{
    ESP_LOGI(TAG, "pdl_board_init start");

    pdl_board_config_pins();

#if PDL_HAS_RTC_DS3231M
    ESP_LOGI(TAG, "RTC DS3231M capability enabled");
    /* RTC driver init is handled by hal_rtc_ds3231m.c */
#endif

#if PDL_HAS_DISPLAY_ST7796
    ESP_LOGI(TAG, "ST7796 display capability enabled");
    /* Display driver init handled by hal_display_st7796s.c */
#endif

    ESP_LOGI(TAG, "pdl_board_init done");
    return ESP_OK;
}
