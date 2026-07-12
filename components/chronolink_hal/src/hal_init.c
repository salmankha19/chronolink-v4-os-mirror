#include "hal.h"
#include "hal_gpio.h"
#include "hal_i2c.h"
#include "hal_spi.h"
#include "hal_rtc.h"
#include "hal_display.h"
#include "hal_sensors.h"
#include "pdl_compat.h"
#include "esp_log.h"

#ifdef CONFIG_DEBUG_DISPLAY_CAPS
#include "esp_log.h"

static const char *DISPLAY_CAP_TAG = "DISPLAY_CAPS";

static const char *cap_name(hal_display_cap_t cap)
{
    switch (cap) {
    case HAL_CAP_DRAW_PIXEL:   return "DRAW_PIXEL";
    case HAL_CAP_FILL:         return "FILL";
    case HAL_CAP_CLEAR:        return "CLEAR";
    case HAL_CAP_SHOW:         return "SHOW";
    case HAL_CAP_TEXT:         return "TEXT";
    case HAL_CAP_BITMAP:       return "BITMAP";
    case HAL_CAP_BRIGHTNESS:   return "BRIGHTNESS";
    case HAL_CAP_ROTATION:     return "ROTATION";
    default:                   return "UNKNOWN";
    }
}

static void log_display_caps(void)
{
    hal_display_cap_t caps[] = {
        HAL_CAP_DRAW_PIXEL, HAL_CAP_FILL, HAL_CAP_CLEAR, HAL_CAP_SHOW,
        HAL_CAP_TEXT, HAL_CAP_BITMAP, HAL_CAP_BRIGHTNESS, HAL_CAP_ROTATION
    };

    for (size_t i = 0; i < sizeof(caps)/sizeof(caps[0]); ++i) {
        hal_status_t s = HAL_Display_HasCapability(caps[i]);
        ESP_LOGI(DISPLAY_CAP_TAG, "%s -> %s", cap_name(caps[i]),
                 (s == HAL_OK) ? "OK" : "NOT SUPPORTED");
    }
}
#endif /* CONFIG_DEBUG_DISPLAY_CAPS */

static const char *TAG = "HAL_INIT";

hal_status_t HAL_Init(void)
{
    hal_status_t hs;

    /* Board level platform init (PDL). Keep existing behavior that returns esp_err_t */
    if (pdl_board_init() != ESP_OK) {
        ESP_LOGE(TAG, "pdl_board_init failed");
        return HAL_ERR_INIT;
    }

    /* GPIO is foundational; treat failure as fatal */
    HAL_GPIO_Init();

    /* I2C */
    hs = HAL_I2C_Init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "HAL_I2C_Init failed (%d)", (int)hs);
        return hs;
    }

    /* SPI wrapper does not strictly require an init on some platforms,
       but if you implement HAL_SPI_Init later, check it here. If no init exists,
       we still rely on HAL_SPI_Transfer to initialize the bus as needed. */
#ifdef HAVE_HAL_SPI_INIT
    hs = HAL_SPI_Init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "HAL_SPI_Init failed (%d)", (int)hs);
        return hs;
    }
#endif

    /* RTC */
    hs = HAL_RTC_Init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "HAL_RTC_Init failed (%d)", (int)hs);
        return hs;
    }

    /* Display */
    hs = HAL_Display_Init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "HAL_Display_Init failed (%d)", (int)hs);
        return hs;
    }
    
#ifdef CONFIG_DEBUG_DISPLAY_CAPS
    log_display_caps();
#endif

    /* Sensors (optional) */
    hs = HAL_Sensors_Init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "HAL_Sensors_Init failed (%d)", (int)hs);
        return hs;
    }

    ESP_LOGI(TAG, "HAL initialized successfully");
    return HAL_OK;
}
