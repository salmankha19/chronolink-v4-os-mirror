#include "pdl_pins.h"
#include "pdl_capabilities.h"
#include "pdl_partitions.h"
#include "hal.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "pdl_board";

void pdl_board_config_pins(void)
{
    ESP_LOGI(TAG, "Configuring board pins");

    /* Status LED */
#ifdef PDL_PIN_LED_STATUS
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PDL_PIN_LED_STATUS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(PDL_PIN_LED_STATUS, 0);
#endif

    /* I2C pins (SDA, SCL) - leave as inputs if not used by driver init */
#ifdef PDL_PIN_I2C_SDA
    io_conf.pin_bit_mask = (1ULL << PDL_PIN_I2C_SDA) | (1ULL << PDL_PIN_I2C_SCL);
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
#endif

    /* SPI pins */
#ifdef PDL_PIN_SPI_MOSI
    io_conf.pin_bit_mask = (1ULL << PDL_PIN_SPI_MOSI) | (1ULL << PDL_PIN_SPI_MISO) | (1ULL << PDL_PIN_SPI_SCLK);
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
#endif

    /* Display control pins */
#ifdef PDL_PIN_DISPLAY_DC
    io_conf.pin_bit_mask = (1ULL << PDL_PIN_DISPLAY_DC) | (1ULL << PDL_PIN_DISPLAY_RST);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(PDL_PIN_DISPLAY_RST, 1);
#endif

    /* Backlight */
#ifdef PDL_PIN_DISPLAY_BL
    io_conf.pin_bit_mask = (1ULL << PDL_PIN_DISPLAY_BL);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(PDL_PIN_DISPLAY_BL, 1);
#endif

    /* RTC interrupt pin */
#ifdef PDL_PIN_RTC_INT
    io_conf.pin_bit_mask = (1ULL << PDL_PIN_RTC_INT);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&io_conf);
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