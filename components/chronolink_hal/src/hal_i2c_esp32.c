#include "hal_i2c.h"
#include "board_pins.h"
#include "pdl_pins.h"
#include "esp_log.h"
#include "driver/i2c.h"

static const char *TAG = "HAL_I2C";
static bool i2c_ready = false;
static bool i2c_disabled = false;
static int s_i2c_sda = -1;
static int s_i2c_scl = -1;

#ifndef HAL_I2C_PORT
#define HAL_I2C_PORT I2C_NUM_0
#endif

#ifndef HAL_I2C_FREQ_HZ
#define HAL_I2C_FREQ_HZ 400000
#endif

#ifndef HAL_I2C_TIMEOUT_MS
#define HAL_I2C_TIMEOUT_MS 100
#endif

#ifndef I2C_FALLBACK_SDA
#define I2C_FALLBACK_SDA 21
#endif

#ifndef I2C_FALLBACK_SCL
#define I2C_FALLBACK_SCL 26
#endif

static bool is_valid_i2c_gpio(int pin)
{
    return GPIO_IS_VALID_GPIO(pin) && GPIO_IS_VALID_OUTPUT_GPIO(pin);
}

static bool hal_i2c_pins_valid(void)
{
    if (is_valid_i2c_gpio(BOARD_I2C_SDA)) {
        s_i2c_sda = BOARD_I2C_SDA;
    } else if (is_valid_i2c_gpio(PDL_PIN_I2C_SDA)) {
        ESP_LOGE("PINCHK", "Invalid BOARD_I2C_SDA=%d, fallback PDL_PIN_I2C_SDA=%d", BOARD_I2C_SDA, PDL_PIN_I2C_SDA);
        s_i2c_sda = PDL_PIN_I2C_SDA;
    } else {
        ESP_LOGE("PINCHK", "Invalid BOARD/PDL I2C SDA (%d/%d), fallback to %d", BOARD_I2C_SDA, PDL_PIN_I2C_SDA, I2C_FALLBACK_SDA);
        s_i2c_sda = I2C_FALLBACK_SDA;
    }

    if (is_valid_i2c_gpio(BOARD_I2C_SCL)) {
        s_i2c_scl = BOARD_I2C_SCL;
    } else if (is_valid_i2c_gpio(PDL_PIN_I2C_SCL)) {
        ESP_LOGE("PINCHK", "Invalid BOARD_I2C_SCL=%d, fallback PDL_PIN_I2C_SCL=%d", BOARD_I2C_SCL, PDL_PIN_I2C_SCL);
        s_i2c_scl = PDL_PIN_I2C_SCL;
    } else {
        ESP_LOGE("PINCHK", "Invalid BOARD/PDL I2C SCL (%d/%d), fallback to %d", BOARD_I2C_SCL, PDL_PIN_I2C_SCL, I2C_FALLBACK_SCL);
        s_i2c_scl = I2C_FALLBACK_SCL;
    }

    if (!is_valid_i2c_gpio(s_i2c_sda) || !is_valid_i2c_gpio(s_i2c_scl)) {
        ESP_LOGE("PINCHK", "Invalid effective I2C pins SDA=%d SCL=%d", s_i2c_sda, s_i2c_scl);
        return false;
    }

    ESP_LOGI("PINDBG", "I2C effective pins SDA=%d SCL=%d (BOARD_SDA=%d BOARD_SCL=%d)",
             s_i2c_sda, s_i2c_scl, BOARD_I2C_SDA, BOARD_I2C_SCL);
    return true;
}

hal_status_t HAL_I2C_Init(void)
{
    if (i2c_disabled) {
        return HAL_OK;
    }

    if (i2c_ready) {
        return HAL_OK;
    }

    if (!hal_i2c_pins_valid()) {
        i2c_disabled = true;
        return HAL_OK;
    }

    const i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = s_i2c_sda,
        .scl_io_num = s_i2c_scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = HAL_I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(HAL_I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return HAL_ERR_INIT;
    }

    err = i2c_driver_install(HAL_I2C_PORT, conf.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        i2c_ready = true;
        return HAL_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return HAL_ERR_INIT;
    }

    i2c_ready = true;
    ESP_LOGI(TAG, "I2C master initialized on SDA=%d SCL=%d", s_i2c_sda, s_i2c_scl);
    return HAL_OK;
}

hal_status_t HAL_I2C_Read(uint8_t dev_addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (i2c_disabled) {
        return HAL_ERR_INIT;
    }

    if (!buf || len == 0) {
        return HAL_ERR_DEV;
    }
    if (HAL_I2C_Init() != HAL_OK) {
        return HAL_ERR_INIT;
    }

    const esp_err_t err = i2c_master_write_read_device(
        HAL_I2C_PORT,
        dev_addr,
        &reg,
        1,
        buf,
        len,
        pdMS_TO_TICKS(HAL_I2C_TIMEOUT_MS)
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed (addr=0x%02x reg=0x%02x): %s", dev_addr, reg, esp_err_to_name(err));
        return HAL_ERR_DEV;
    }

    return HAL_OK;
}

hal_status_t HAL_I2C_Write(uint8_t dev_addr, uint8_t reg, const uint8_t *data, uint16_t len)
{
    if (i2c_disabled) {
        return HAL_ERR_INIT;
    }

    if (len > 0 && !data) {
        return HAL_ERR_DEV;
    }
    if (HAL_I2C_Init() != HAL_OK) {
        return HAL_ERR_INIT;
    }

    uint8_t stack_buf[64];
    uint8_t *tx = stack_buf;
    const uint16_t tx_len = (uint16_t)(len + 1);

    if (tx_len > sizeof(stack_buf)) {
        return HAL_ERR_DEV;
    }

    tx[0] = reg;
    for (uint16_t i = 0; i < len; ++i) {
        tx[i + 1] = data[i];
    }

    const esp_err_t err = i2c_master_write_to_device(
        HAL_I2C_PORT,
        dev_addr,
        tx,
        tx_len,
        pdMS_TO_TICKS(HAL_I2C_TIMEOUT_MS)
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed (addr=0x%02x reg=0x%02x): %s", dev_addr, reg, esp_err_to_name(err));
        return HAL_ERR_DEV;
    }

    return HAL_OK;
}
