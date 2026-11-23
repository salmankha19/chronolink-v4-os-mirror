#include "hal_i2c.h"
#include "pdl_pins.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "HAL_I2C";
static bool i2c_ready = false;
static bool i2c_disabled = false;
static int s_i2c_sda = -1;
static int s_i2c_scl = -1;

/* Guards every transaction on the shared I2C bus (RTC, SHT4x, VEML7700, ...).
   Sensors are polled from the UI task on Core 1 while the RTC or other
   services may touch the bus from Core 0 — without this, interleaved
   transactions can corrupt each other's register reads. */
static SemaphoreHandle_t s_i2c_mutex = NULL;

#define I2C_LOCK_TIMEOUT_MS 200

static inline bool i2c_lock(void)
{
    /* Fail closed if the mutex was never created. A missing mutex means
       HAL_I2C_Init() did not run (or ran with invalid pins and disabled
       the bus), so any transfer attempt would proceed unguarded. Return
       false -- callers already treat false as HAL_ERR_BUS. See PR 4. */
    if (!s_i2c_mutex) {
        return false;
    }
    return xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(I2C_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static inline void i2c_unlock(void)
{
    if (s_i2c_mutex) {
        xSemaphoreGive(s_i2c_mutex);
    }
}

#ifndef HAL_I2C_PORT
#define HAL_I2C_PORT I2C_NUM_0
#endif

#ifndef HAL_I2C_FREQ_HZ
#define HAL_I2C_FREQ_HZ 400000
#endif

#ifndef HAL_I2C_TIMEOUT_MS
#define HAL_I2C_TIMEOUT_MS 100
#endif

/* The I2C pins come from the PDL (pdl_pins.h) and from nowhere else.
   There is deliberately no runtime fallback: if the PDL reports the bus as
   not populated (-1) or names a pad that cannot drive SDA/SCL, the HAL
   reports the configuration error and leaves the bus down. Substituting
   pins would mean driving a pad that belongs to another function on the
   PCB. See docs/hal.md "Init order". */

static bool is_valid_i2c_gpio(int pin)
{
    return pin >= 0 && pin < GPIO_NUM_MAX &&
           GPIO_IS_VALID_GPIO(pin) && GPIO_IS_VALID_OUTPUT_GPIO(pin);
}

static bool hal_i2c_resolve_pins(void)
{
    s_i2c_sda = PDL_PIN_I2C_SDA;
    s_i2c_scl = PDL_PIN_I2C_SCL;

    if (!is_valid_i2c_gpio(s_i2c_sda) || !is_valid_i2c_gpio(s_i2c_scl)) {
        ESP_LOGE(TAG, "Invalid I2C pins from pdl_pins.h: SDA=%d SCL=%d "
                      "(valid range 0..%d) -- refusing to initialise the bus",
                 s_i2c_sda, s_i2c_scl, (int)(GPIO_NUM_MAX - 1));
        return false;
    }

    ESP_LOGI(TAG, "I2C pins SDA=%d SCL=%d", s_i2c_sda, s_i2c_scl);
    return true;
}

hal_status_t HAL_I2C_Init(void)
{
    if (i2c_disabled) {
        /* Pin validation failed on an earlier call. Stay failed: returning
           HAL_OK here would tell callers a bus exists when it does not. */
        return HAL_ERR_INIT;
    }

    if (i2c_ready) {
        return HAL_OK;
    }

    if (!hal_i2c_resolve_pins()) {
        i2c_disabled = true;
        return HAL_ERR_INIT;
    }

    if (!s_i2c_mutex) {
        s_i2c_mutex = xSemaphoreCreateMutex();
        if (!s_i2c_mutex) {
            ESP_LOGE(TAG, "Failed to create I2C bus mutex");
            return HAL_ERR_INIT;
        }
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

    if (!i2c_lock()) {
        ESP_LOGW(TAG, "I2C bus busy, read timed out (addr=0x%02x reg=0x%02x)", dev_addr, reg);
        return HAL_ERR_BUS;
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
    i2c_unlock();

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

    if (!i2c_lock()) {
        ESP_LOGW(TAG, "I2C bus busy, write timed out (addr=0x%02x reg=0x%02x)", dev_addr, reg);
        return HAL_ERR_BUS;
    }

    const esp_err_t err = i2c_master_write_to_device(
        HAL_I2C_PORT,
        dev_addr,
        tx,
        tx_len,
        pdMS_TO_TICKS(HAL_I2C_TIMEOUT_MS)
    );
    i2c_unlock();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed (addr=0x%02x reg=0x%02x): %s", dev_addr, reg, esp_err_to_name(err));
        return HAL_ERR_DEV;
    }

    return HAL_OK;
}

hal_status_t HAL_I2C_Transmit(uint8_t dev_addr,
                              const uint8_t *data,
                              uint16_t len)
{
    if (i2c_disabled) return HAL_ERR_INIT;
    if (len > 0 && !data) return HAL_ERR_DEV;
    if (HAL_I2C_Init() != HAL_OK) return HAL_ERR_INIT;

    if (!i2c_lock()) {
        ESP_LOGW(TAG, "I2C bus busy, transmit timed out (addr=0x%02x)", dev_addr);
        return HAL_ERR_BUS;
    }

    esp_err_t err = i2c_master_write_to_device(
        HAL_I2C_PORT, dev_addr, data, len,
        pdMS_TO_TICKS(HAL_I2C_TIMEOUT_MS));
    i2c_unlock();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transmit failed (addr=0x%02x): %s", dev_addr, esp_err_to_name(err));
        return HAL_ERR_DEV;
    }
    return HAL_OK;
}

hal_status_t HAL_I2C_Receive(uint8_t dev_addr,
                             uint8_t *buf,
                             uint16_t len)
{
    if (i2c_disabled) return HAL_ERR_INIT;
    if (!buf || len == 0) return HAL_ERR_DEV;
    if (HAL_I2C_Init() != HAL_OK) return HAL_ERR_INIT;

    if (!i2c_lock()) {
        ESP_LOGW(TAG, "I2C bus busy, receive timed out (addr=0x%02x)", dev_addr);
        return HAL_ERR_BUS;
    }

    esp_err_t err = i2c_master_read_from_device(
        HAL_I2C_PORT, dev_addr, buf, len,
        pdMS_TO_TICKS(HAL_I2C_TIMEOUT_MS));
    i2c_unlock();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Receive failed (addr=0x%02x): %s", dev_addr, esp_err_to_name(err));
        return HAL_ERR_DEV;
    }
    return HAL_OK;
}

hal_status_t HAL_I2C_TransmitReceive(uint8_t dev_addr,
                                      const uint8_t *tx_data,
                                      uint16_t tx_len,
                                      uint8_t *rx_buf,
                                      uint16_t rx_len)
{
    if (i2c_disabled) return HAL_ERR_INIT;
    if ((tx_len > 0 && !tx_data) || !rx_buf || rx_len == 0) return HAL_ERR_DEV;
    if (HAL_I2C_Init() != HAL_OK) return HAL_ERR_INIT;

    if (!i2c_lock()) {
        ESP_LOGW(TAG, "I2C bus busy, transmit/receive timed out (addr=0x%02x)", dev_addr);
        return HAL_ERR_BUS;
    }

    esp_err_t err = i2c_master_write_read_device(
        HAL_I2C_PORT,
        dev_addr,
        tx_data,
        tx_len,
        rx_buf,
        rx_len,
        pdMS_TO_TICKS(HAL_I2C_TIMEOUT_MS));
    i2c_unlock();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TransmitReceive failed (addr=0x%02x): %s", dev_addr, esp_err_to_name(err));
        return HAL_ERR_DEV;
    }
    return HAL_OK;
}