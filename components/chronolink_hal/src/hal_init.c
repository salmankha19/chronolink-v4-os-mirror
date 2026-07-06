#include "hal.h"
#include "hal_gpio.h"
#include "hal_i2c.h"
#include "hal_spi.h"
#include "hal_rtc.h"
#include "hal_display.h"
#include "hal_sensors.h"
#include "pdl_compat.h"
#include "esp_log.h"

static const char *TAG = "HAL_INIT";

/* Simple initializer: call sub-inits and return success.
   If you later change sub-inits to return status, switch to checking them. */
hal_status_t HAL_Init(void)
{
    if (pdl_board_init() != ESP_OK) {
        ESP_LOGE(TAG, "pdl_board_init failed");
        return HAL_ERR_INIT;
    }

    HAL_GPIO_Init();
    HAL_I2C_Init();
    HAL_SPI_Init();
    HAL_RTC_Init();
    HAL_Display_Init();
    HAL_Sensors_Init();
    return HAL_OK;
}
