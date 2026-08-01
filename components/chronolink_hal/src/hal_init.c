#include "hal.h"
#include "hal_gpio.h"
#include "hal_i2c.h"
#include "hal_spi.h"
#include "hal_rtc.h"
#include "hal_display.h"
#include "hal_sensors.h"
#include "hal_sensor_bme280.h"
#include "hal_sensor_sht4x.h"
#include "hal_sensor_veml7700.h"
#include "pdl_compat.h"
#include "esp_log.h"
#include "esp_err.h"

/* === ChronoLink V4 Display Subsystem === */
/* Uncomment/adjust these once your generated headers are in the include path */
#ifdef CONFIG_CHRONOLINK_V4_DISPLAY
// #include "hal_display_manager.h"
// #include "hal_display_router.h"
// #include "hal_gfx.h"
#endif

bool valid_gpio(int pin);

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
    case HAL_CAP_ORIENTATION:  return "ORIENTATION";
    default:                   return "UNKNOWN";
    }
}

static void log_display_caps(void)
{
    hal_display_cap_t caps[] = {
        HAL_CAP_DRAW_PIXEL, HAL_CAP_FILL, HAL_CAP_CLEAR, HAL_CAP_SHOW,
        HAL_CAP_TEXT, HAL_CAP_BITMAP, HAL_CAP_BRIGHTNESS, HAL_CAP_ORIENTATION
    };

    for (size_t i = 0; i < sizeof(caps)/sizeof(caps[0]); ++i) {
        hal_status_t s = HAL_Display_HasCapability(caps[i]);
        ESP_LOGI(DISPLAY_CAP_TAG, "%s -> %s", cap_name(caps[i]),
                 (s == HAL_OK) ? "OK" : "NOT SUPPORTED");
    }
}
#endif /* CONFIG_DEBUG_DISPLAY_CAPS */

static const char *TAG = "HAL_INIT";

/* ensure the keep symbol is referenced so the linker keeps it */
#ifdef CONFIG_CHRONOLINK_USE_MOCK_DISPLAY
extern const char keep_display_caps[];
static void __attribute__((constructor)) keep_display_caps_ref_init(void)
{
    volatile const char *p = keep_display_caps;
    (void)p;
}
#endif


hal_status_t HAL_Init(void)
{
    hal_status_t hs;

    /* Board level platform init (PDL). Keep existing behavior that returns esp_err_t */
    esp_err_t board_err = pdl_board_init();
    if (board_err != ESP_OK) {
        ESP_LOGE(TAG, "pdl_board_init failed: %s (%d)", esp_err_to_name(board_err), (int)board_err);
        return HAL_ERR_INIT;
    }

    /* GPIO is foundational; treat failure as fatal */
    HAL_GPIO_Init();

    /* I2C */
    if (valid_gpio(PDL_PIN_I2C_SCL) && valid_gpio(PDL_PIN_I2C_SDA)) {
        hs = HAL_I2C_Init();
        if (hs != HAL_OK) {
            ESP_LOGE(TAG, "HAL_I2C_Init failed (%d)", (int)hs);
            return hs;
        }
    } else {
        ESP_LOGW(TAG, "Skipping I2C init due to invalid pins SCL=%d SDA=%d",
                 PDL_PIN_I2C_SCL, PDL_PIN_I2C_SDA);
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

    /* ============================================================
     * ChronoLink V4 Display Subsystem
     * ============================================================
     * Order matters: Router -> Manager -> GFX -> Concrete Display
     * Adjust if your generated code uses a different init sequence.
     */
#ifdef CONFIG_CHRONOLINK_V4_DISPLAY
    /* 1. Display Router (hybrid driver resolution) */
    // hs = HAL_DisplayRouter_Init();
    // if (hs != HAL_OK) {
    //     ESP_LOGE(TAG, "HAL_DisplayRouter_Init failed (%d)", (int)hs);
    //     return hs;
    // }

    /* 2. Display Manager (if applicable) */
    // hs = HAL_DisplayManager_Init();
    // if (hs != HAL_OK) {
    //     ESP_LOGE(TAG, "HAL_DisplayManager_Init failed (%d)", (int)hs);
    //     return hs;
    // }

    /* 3. GFX runtime resolution (64 min, 480 max) */
    /* TODO: Replace with runtime-detected or Kconfig-derived width/height */
    // const uint16_t gfx_width  = CONFIG_CHRONOLINK_GFX_WIDTH;
    // const uint16_t gfx_height = CONFIG_CHRONOLINK_GFX_HEIGHT;
    // if (gfx_width < 64 || gfx_width > 480 || gfx_height < 64 || gfx_height > 480) {
    //     ESP_LOGE(TAG, "GFX dimensions out of bounds: %dx%d", gfx_width, gfx_height);
    //     return HAL_ERR_INIT;
    // }
    // hs = HAL_GFX_Init(gfx_width, gfx_height);
    // if (hs != HAL_OK) {
    //     ESP_LOGE(TAG, "HAL_GFX_Init(%d,%d) failed (%d)", gfx_width, gfx_height, (int)hs);
    //     return hs;
    // }
    // ESP_LOGI(TAG, "GFX initialized @ %dx%d", gfx_width, gfx_height);

    /* 4. Concrete display driver (ST7796S built-in, JSON executor, etc.) */
    hs = HAL_Display_Init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "HAL_Display_Init failed (%d)", (int)hs);
        return hs;
    }
#else
    /* Legacy display init path */
    hs = HAL_Display_Init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "HAL_Display_Init failed (%d)", (int)hs);
        return hs;
    }
#endif /* CONFIG_CHRONOLINK_V4_DISPLAY */

#ifdef CONFIG_DEBUG_DISPLAY_CAPS
    log_display_caps();
#endif

    /* Sensors (optional) */
    (void)HAL_Sensor_Register(&HAL_SENSOR_BME280_DRIVER);
    (void)HAL_Sensor_Register(&HAL_SENSOR_SHT4X_DRIVER);
    (void)HAL_Sensor_Register(&HAL_SENSOR_VEML7700_DRIVER);

    hs = HAL_Sensors_Init();
    if (hs != HAL_OK) {
        ESP_LOGE(TAG, "HAL_Sensors_Init failed (%d)", (int)hs);
        return hs;
    }

    ESP_LOGI(TAG, "HAL initialized successfully");
    return HAL_OK;
}