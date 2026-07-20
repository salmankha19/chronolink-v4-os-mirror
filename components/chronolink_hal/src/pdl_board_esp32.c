#include "pdl_board.h"
#include "pdl_pins.h"
#include "pdl_capabilities.h"
#include "pdl_partitions.h"
#include "hal.h"
#include "hal_gpio_safe.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "pdl_board";

#if defined(PDL_PIN_LED_STATUS)
#define PINDBG_LED_STATUS PDL_PIN_LED_STATUS
#else
#define PINDBG_LED_STATUS -1
#endif

#if defined(PDL_PIN_I2C_SDA)
#define PINDBG_I2C_SDA PDL_PIN_I2C_SDA
#else
#define PINDBG_I2C_SDA -1
#endif

#if defined(PDL_PIN_I2C_SCL)
#define PINDBG_I2C_SCL PDL_PIN_I2C_SCL
#else
#define PINDBG_I2C_SCL -1
#endif

#if defined(PDL_PIN_SPI_MOSI)
#define PINDBG_SPI_MOSI PDL_PIN_SPI_MOSI
#else
#define PINDBG_SPI_MOSI -1
#endif

#if defined(PDL_PIN_SPI_MISO)
#define PINDBG_SPI_MISO PDL_PIN_SPI_MISO
#else
#define PINDBG_SPI_MISO -1
#endif

#if defined(PDL_PIN_SPI_SCLK)
#define PINDBG_SPI_SCLK PDL_PIN_SPI_SCLK
#else
#define PINDBG_SPI_SCLK -1
#endif

#if defined(PDL_PIN_SPI_CS_DISPLAY)
#define PINDBG_SPI_CS_DISPLAY PDL_PIN_SPI_CS_DISPLAY
#else
#define PINDBG_SPI_CS_DISPLAY -1
#endif

#if defined(PDL_PIN_DISPLAY_DC)
#define PINDBG_DISPLAY_DC PDL_PIN_DISPLAY_DC
#else
#define PINDBG_DISPLAY_DC -1
#endif

#if defined(PDL_PIN_DISPLAY_RST)
#define PINDBG_DISPLAY_RST PDL_PIN_DISPLAY_RST
#else
#define PINDBG_DISPLAY_RST -1
#endif

#if defined(PDL_PIN_DISPLAY_BL)
#define PINDBG_DISPLAY_BL PDL_PIN_DISPLAY_BL
#else
#define PINDBG_DISPLAY_BL -1
#endif

#if defined(PDL_PIN_RTC_INT)
#define PINDBG_RTC_INT PDL_PIN_RTC_INT
#else
#define PINDBG_RTC_INT -1
#endif

#if defined(PDL_PIN_DF_TX)
#define PINDBG_DF_TX PDL_PIN_DF_TX
#else
#define PINDBG_DF_TX -1
#endif

#if defined(PDL_PIN_DF_RX)
#define PINDBG_DF_RX PDL_PIN_DF_RX
#else
#define PINDBG_DF_RX -1
#endif

#if defined(PDL_PIN_DF_BUSY)
#define PINDBG_DF_BUSY PDL_PIN_DF_BUSY
#else
#define PINDBG_DF_BUSY -1
#endif

#if defined(PDL_PIN_DF_RESET)
#define PINDBG_DF_RESET PDL_PIN_DF_RESET
#else
#define PINDBG_DF_RESET -1
#endif

#if defined(PDL_PIN_BUF_EN)
#define PINDBG_BUF_EN PDL_PIN_BUF_EN
#else
#define PINDBG_BUF_EN -1
#endif

#if defined(PDL_PIN_BUFFER_EN)
#define PINDBG_BUFFER_EN PDL_PIN_BUFFER_EN
#else
#define PINDBG_BUFFER_EN -1
#endif

#if defined(PDL_PIN_MOSFET_GATE)
#define PINDBG_MOSFET_GATE PDL_PIN_MOSFET_GATE
#else
#define PINDBG_MOSFET_GATE -1
#endif

#if defined(PDL_PIN_SAFE_MODE)
#define PINDBG_SAFE_MODE PDL_PIN_SAFE_MODE
#else
#define PINDBG_SAFE_MODE -1
#endif

#ifndef GPIO_PIN_COUNT
#define GPIO_PIN_COUNT 48
#endif

static bool validate_pin(int pin, const char *name)
{
    if (pin >= 0 && pin <= 47 && GPIO_IS_VALID_GPIO(pin)) {
        return true;
    }

    ESP_LOGE("PINCHK", "Invalid pin for %s: %d", name, pin);
    return false;
}

static bool validate_output_pin(int pin, const char *name)
{
    if (!validate_pin(pin, name)) {
        return false;
    }

    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        ESP_LOGE("PINCHK", "Pin for %s is not output-capable: %d", name, pin);
        return false;
    }

    return true;
}

static bool validate_pin_optional(int pin, const char *name)
{
    if (pin >= 0 && pin <= 47 && GPIO_IS_VALID_GPIO(pin)) {
        return true;
    }

    return false;
}

/* safe_pin_mask: return 0 for invalid pins and log a warning
 * Prevents shifting by negative or out-of-range values which produces
 * huge masks that later cause gpio_config/gpio_set_level errors.
 */
static uint64_t safe_pin_mask(int pin, const char *name)
{
    if (!validate_pin(pin, name)) {
        ESP_LOGW("PINCHK", "Mask skipped for %s due to invalid pin %d", name, pin);
        return 0ULL;
    }
    return (1ULL << pin);
}

static uint64_t safe_pin_mask_optional(int pin, const char *name)
{
    if (pin >= 0 && pin <= 47 && GPIO_IS_VALID_GPIO(pin)) {
        return (1ULL << pin);
    }

    return 0ULL;
}

static void log_pindbg_line(void)
{
    ESP_LOGI("PINDBG",
             "PIN_LED_STATUS=%d PIN_I2C_SDA=%d PIN_I2C_SCL=%d PIN_SPI_MOSI=%d PIN_SPI_MISO=%d PIN_SPI_SCLK=%d PIN_SPI_CS_DISPLAY=%d PIN_DISPLAY_DC=%d PIN_DISPLAY_RST=%d PIN_DISPLAY_BL=%d PIN_RTC_INT=%d PIN_DF_TX=%d PIN_DF_RX=%d PIN_DF_BUSY=%d PIN_DF_RESET=%d PIN_BUF_EN=%d PIN_BUFFER_EN=%d PIN_MOSFET_GATE=%d PIN_SAFE_MODE=%d",
             PINDBG_LED_STATUS,
             PINDBG_I2C_SDA,
             PINDBG_I2C_SCL,
             PINDBG_SPI_MOSI,
             PINDBG_SPI_MISO,
             PINDBG_SPI_SCLK,
             PINDBG_SPI_CS_DISPLAY,
             PINDBG_DISPLAY_DC,
             PINDBG_DISPLAY_RST,
             PINDBG_DISPLAY_BL,
             PINDBG_RTC_INT,
             PINDBG_DF_TX,
             PINDBG_DF_RX,
             PINDBG_DF_BUSY,
             PINDBG_DF_RESET,
             PINDBG_BUF_EN,
             PINDBG_BUFFER_EN,
             PINDBG_MOSFET_GATE,
             PINDBG_SAFE_MODE);
}

static esp_err_t validate_pdl_pins(void)
{
    bool ok = true;

#ifdef PDL_PIN_LED_STATUS
    ok = validate_output_pin(PDL_PIN_LED_STATUS, "LED_STATUS") && ok;
#endif
#ifdef PDL_PIN_I2C_SDA
    (void)validate_pin_optional(PDL_PIN_I2C_SDA, "I2C_SDA");
#endif
#ifdef PDL_PIN_I2C_SCL
    (void)validate_pin_optional(PDL_PIN_I2C_SCL, "I2C_SCL");
#endif
#ifdef PDL_PIN_SPI_MOSI
    ok = validate_output_pin(PDL_PIN_SPI_MOSI, "SPI_MOSI") && ok;
#endif
#ifdef PDL_PIN_SPI_MISO
    ok = validate_pin(PDL_PIN_SPI_MISO, "SPI_MISO") && ok;
#endif
#ifdef PDL_PIN_SPI_SCLK
    ok = validate_output_pin(PDL_PIN_SPI_SCLK, "SPI_SCLK") && ok;
#endif
#ifdef PDL_PIN_SPI_CS_DISPLAY
    ok = validate_output_pin(PDL_PIN_SPI_CS_DISPLAY, "SPI_CS_DISPLAY") && ok;
#endif
#ifdef PDL_PIN_DISPLAY_DC
    ok = validate_output_pin(PDL_PIN_DISPLAY_DC, "DISPLAY_DC") && ok;
#endif
#ifdef PDL_PIN_DISPLAY_RST
    ok = validate_output_pin(PDL_PIN_DISPLAY_RST, "DISPLAY_RST") && ok;
#endif
#ifdef PDL_PIN_DISPLAY_BL
    ok = validate_output_pin(PDL_PIN_DISPLAY_BL, "DISPLAY_BL") && ok;
#endif
#ifdef PDL_PIN_RTC_INT
    ok = validate_pin(PDL_PIN_RTC_INT, "RTC_INT") && ok;
#endif
#ifdef PDL_PIN_DF_TX
    ok = validate_pin(PDL_PIN_DF_TX, "DF_TX") && ok;
#endif
#ifdef PDL_PIN_DF_RX
    ok = validate_pin(PDL_PIN_DF_RX, "DF_RX") && ok;
#endif
#ifdef PDL_PIN_DF_BUSY
    ok = validate_pin(PDL_PIN_DF_BUSY, "DF_BUSY") && ok;
#endif
#ifdef PDL_PIN_DF_RESET
    ok = validate_pin(PDL_PIN_DF_RESET, "DF_RESET") && ok;
#endif
#ifdef PDL_PIN_BUF_EN
    ok = validate_pin(PDL_PIN_BUF_EN, "BUF_EN") && ok;
#endif
#ifdef PDL_PIN_BUFFER_EN
    ok = validate_pin(PDL_PIN_BUFFER_EN, "BUFFER_EN") && ok;
#endif
#ifdef PDL_PIN_MOSFET_GATE
    ok = validate_pin(PDL_PIN_MOSFET_GATE, "MOSFET_GATE") && ok;
#endif
#ifdef PDL_PIN_SAFE_MODE
    ok = validate_pin(PDL_PIN_SAFE_MODE, "SAFE_MODE") && ok;
#endif

    if (!ok) {
        ESP_LOGE("HWINIT", "pdl_board_init failed: invalid pin(s)");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static esp_err_t pdl_board_config_pins(void)
{
    ESP_LOGI(TAG, "Configuring board pins");

    /* single gpio_config_t used and reinitialized per block */
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));

    /* Status LED */
#ifdef PDL_PIN_LED_STATUS
    memset(&io_conf, 0, sizeof(io_conf));
    {
        uint64_t led_mask = safe_pin_mask(PDL_PIN_LED_STATUS, "LED_STATUS");
        if (led_mask == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        io_conf.pin_bit_mask = led_mask;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_LOGD(TAG, "Config LED_STATUS mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
        board_gpio_set_level(PDL_PIN_LED_STATUS, 0, "PDL_PIN_LED_STATUS");
    }
#endif

    /* I2C pins (SDA, SCL) - leave as inputs if not used by driver init */
#if defined(PDL_PIN_I2C_SDA) && defined(PDL_PIN_I2C_SCL)
    memset(&io_conf, 0, sizeof(io_conf));
    {
        uint64_t sda_mask = safe_pin_mask_optional(PDL_PIN_I2C_SDA, "I2C_SDA");
        uint64_t scl_mask = safe_pin_mask_optional(PDL_PIN_I2C_SCL, "I2C_SCL");
        if (sda_mask == 0 || scl_mask == 0) {
            ESP_LOGD("PINCHK", "Skipping board-level I2C GPIO preconfig; HAL_I2C will configure effective pins");
        } else {
            io_conf.pin_bit_mask = sda_mask | scl_mask;
            io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.intr_type = GPIO_INTR_DISABLE;
            ESP_LOGD(TAG, "Config I2C pins mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
            gpio_config(&io_conf);
        }
    }
#endif

    /* SPI pins */
#ifdef PDL_PIN_SPI_MOSI
    memset(&io_conf, 0, sizeof(io_conf));
    {
        uint64_t mosi_mask = safe_pin_mask(PDL_PIN_SPI_MOSI, "SPI_MOSI");
        uint64_t miso_mask = safe_pin_mask(PDL_PIN_SPI_MISO, "SPI_MISO");
        uint64_t sclk_mask = safe_pin_mask(PDL_PIN_SPI_SCLK, "SPI_SCLK");
        if (mosi_mask == 0 || miso_mask == 0 || sclk_mask == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        io_conf.pin_bit_mask = mosi_mask | miso_mask | sclk_mask;
        io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_LOGD(TAG, "Config SPI pins mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
    }
#endif

    /* Display control pins */
#ifdef PDL_PIN_DISPLAY_DC
    memset(&io_conf, 0, sizeof(io_conf));
    {
        uint64_t dc_mask = safe_pin_mask(PDL_PIN_DISPLAY_DC, "DISPLAY_DC");
        uint64_t rst_mask = safe_pin_mask(PDL_PIN_DISPLAY_RST, "DISPLAY_RST");
        if (dc_mask == 0 || rst_mask == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        io_conf.pin_bit_mask = dc_mask | rst_mask;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_LOGD(TAG, "Config display ctrl mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
        board_gpio_set_level(PDL_PIN_DISPLAY_RST, 1, "PDL_PIN_DISPLAY_RST");
    }
#endif

    /* Backlight */
#ifdef PDL_PIN_DISPLAY_BL
    memset(&io_conf, 0, sizeof(io_conf));
    {
        uint64_t bl_mask = safe_pin_mask(PDL_PIN_DISPLAY_BL, "DISPLAY_BL");
        if (bl_mask == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        io_conf.pin_bit_mask = bl_mask;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_LOGD(TAG, "Config display BL mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
        board_gpio_set_level(PDL_PIN_DISPLAY_BL, 1, "PDL_PIN_DISPLAY_BL");
    }
#endif

    /* RTC interrupt pin */
#ifdef PDL_PIN_RTC_INT
    memset(&io_conf, 0, sizeof(io_conf));
    {
        uint64_t rtc_mask = safe_pin_mask(PDL_PIN_RTC_INT, "RTC_INT");
        if (rtc_mask == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        io_conf.pin_bit_mask = rtc_mask;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_NEGEDGE;
        ESP_LOGD(TAG, "Config RTC_INT mask=0x%llx", (unsigned long long)io_conf.pin_bit_mask);
        gpio_config(&io_conf);
    }
#endif

    ESP_LOGI(TAG, "Board pins configured");
    return ESP_OK;
}

/* Public init called by chronolink_hal during HAL_Init */
esp_err_t pdl_board_init(void)
{
    ESP_LOGI("HWINIT", "pdl_board_init start");

    log_pindbg_line();
    if (validate_pdl_pins() != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    if (pdl_board_config_pins() != ESP_OK) {
        ESP_LOGE("HWINIT", "pdl_board_init failed during pin configuration");
        return ESP_ERR_INVALID_ARG;
    }

#if PDL_HAS_RTC_DS3231M
    ESP_LOGI(TAG, "RTC DS3231M capability enabled");
    /* RTC driver init is handled by hal_rtc_ds3231m.c */
#endif

#if PDL_HAS_DISPLAY_ST7796
    ESP_LOGI(TAG, "ST7796 display capability enabled");
    /* Display driver init handled by hal_display_st7796s.c */
#endif

    ESP_LOGI("HWINIT", "pdl_board_init done");
    return ESP_OK;
}