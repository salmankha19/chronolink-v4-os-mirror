/*
 * hal_display.c
 *
 * ChronoLink V4 OS — Hardware Abstraction Layer
 * Display Router / Backend Dispatcher
 *
 * Responsibilities:
 *  - Compile-time backend selection (Kconfig / board config)
 *  - Thread-safe dispatch to the active backend via FreeRTOS mutex
 *  - State tracking (init, backend type)
 *  - Capability routing
 *
 * Backends are included conditionally so unused drivers are not linked.
 */

#include "sdkconfig.h"
#include "hal_display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* --------------------------------------------------------------------------
 * Conditionally include backend headers
 * -------------------------------------------------------------------------- */
#if defined(CONFIG_BOARD_DISPLAY_ST7796S)
#define BACKEND_ST7796S_ENABLED 1
#include "hal_display_st7796s.h"
#else
#define BACKEND_ST7796S_ENABLED 0
#endif

#if defined(CONFIG_BOARD_DISPLAY_MAX7219)
#define BACKEND_MAX7219_ENABLED 1
#include "hal_display_max7219.h"
#else
#define BACKEND_MAX7219_ENABLED 0
#endif

#if defined(CONFIG_BOARD_DISPLAY_REMOTE)
#define BACKEND_REMOTE_ENABLED 1
#include "hal_display_remote.h"
#else
#define BACKEND_REMOTE_ENABLED 0
#endif

static const char *TAG = "HAL_DISPLAY";

/* --------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------- */
static hal_display_backend_t s_backend = HAL_DISPLAY_BACKEND_NONE;
static bool s_initialized = false;

/* Thread-safety: display is a shared resource; SPI polling is not reentrant */
static SemaphoreHandle_t s_mutex = NULL;

/* --------------------------------------------------------------------------
 * Lock helpers
 * -------------------------------------------------------------------------- */
static inline bool display_lock(void)
{
    if (!s_mutex) return true; /* safety: if mutex not created yet, proceed */
    return (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE);
}

static inline void display_unlock(void)
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

/* --------------------------------------------------------------------------
 * Backend selection (compile-time via Kconfig)
 * -------------------------------------------------------------------------- */
static hal_display_backend_t hal_display_select_backend(void)
{
#if BACKEND_ST7796S_ENABLED
    return HAL_DISPLAY_BACKEND_ST7796S;
#elif BACKEND_MAX7219_ENABLED
    return HAL_DISPLAY_BACKEND_MAX7219;
#elif BACKEND_REMOTE_ENABLED
    return HAL_DISPLAY_BACKEND_REMOTE;
#else
    return HAL_DISPLAY_BACKEND_NONE;
#endif
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

hal_status_t HAL_Display_Init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return HAL_OK;
    }

    s_backend = hal_display_select_backend();
    if (s_backend == HAL_DISPLAY_BACKEND_NONE) {
        ESP_LOGW(TAG, "No display backend selected at compile time");
        /* Soft-fail: system can run headless */
        return HAL_OK;
    }

    /* Create mutex once */
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            ESP_LOGE(TAG, "Failed to create display mutex");
            return HAL_ERR_INIT;
        }
    }

    hal_status_t status;
    display_lock();
    switch (s_backend) {
#if BACKEND_ST7796S_ENABLED
    case HAL_DISPLAY_BACKEND_ST7796S:
        ESP_LOGI(TAG, "Initializing ST7796S display");
        status = HAL_Display_ST7796S_Init();
        break;
#endif
#if BACKEND_MAX7219_ENABLED
    case HAL_DISPLAY_BACKEND_MAX7219:
        ESP_LOGI(TAG, "Initializing MAX7219 display");
        status = HAL_Display_MAX7219_Init();
        break;
#endif
#if BACKEND_REMOTE_ENABLED
    case HAL_DISPLAY_BACKEND_REMOTE:
        ESP_LOGI(TAG, "Initializing Remote Display backend");
        status = HAL_Display_Remote_Init();
        break;
#endif
    default:
        status = HAL_ERR_INIT;
        break;
    }
    display_unlock();

    if (status == HAL_OK) {
        s_initialized = true;
        ESP_LOGI(TAG, "Display initialized (backend=%d)", (int)s_backend);
    } else {
        ESP_LOGE(TAG, "Backend init failed: %d", (int)status);
    }

    return status;
}

hal_status_t HAL_Display_Deinit(void)
{
    if (!s_initialized) {
        return HAL_OK;
    }

    hal_status_t status;
    display_lock();
    switch (s_backend) {
#if BACKEND_ST7796S_ENABLED
    case HAL_DISPLAY_BACKEND_ST7796S:
        status = HAL_Display_ST7796S_Deinit();
        break;
#endif
#if BACKEND_MAX7219_ENABLED
    case HAL_DISPLAY_BACKEND_MAX7219:
        status = HAL_Display_MAX7219_Deinit();
        break;
#endif
#if BACKEND_REMOTE_ENABLED
    case HAL_DISPLAY_BACKEND_REMOTE:
        status = HAL_Display_Remote_Deinit();
        break;
#endif
    default:
        status = HAL_OK;
        break;
    }
    display_unlock();

    s_initialized = false;
    s_backend = HAL_DISPLAY_BACKEND_NONE;

    ESP_LOGI(TAG, "Display deinitialized");
    return status;
}

hal_status_t HAL_Display_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    if (!s_initialized) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_DRAW_PIXEL) != HAL_OK)
        return HAL_ERR_DEV;

    hal_status_t status;
    display_lock();
    switch (s_backend) {
#if BACKEND_ST7796S_ENABLED
    case HAL_DISPLAY_BACKEND_ST7796S:
        status = HAL_Display_ST7796S_DrawPixel(x, y, color);
        break;
#endif
#if BACKEND_MAX7219_ENABLED
    case HAL_DISPLAY_BACKEND_MAX7219:
        status = HAL_Display_MAX7219_DrawPixel(x, y, color);
        break;
#endif
#if BACKEND_REMOTE_ENABLED
    case HAL_DISPLAY_BACKEND_REMOTE:
        status = HAL_Display_Remote_DrawPixel(x, y, color);
        break;
#endif
    default:
        status = HAL_ERR_DEV;
        break;
    }
    display_unlock();
    return status;
}

hal_status_t HAL_Display_Fill(uint32_t color)
{
    if (!s_initialized) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_FILL) != HAL_OK)
        return HAL_ERR_DEV;

    hal_status_t status;
    display_lock();
    switch (s_backend) {
#if BACKEND_ST7796S_ENABLED
    case HAL_DISPLAY_BACKEND_ST7796S:
        status = HAL_Display_ST7796S_Fill(color);
        break;
#endif
#if BACKEND_MAX7219_ENABLED
    case HAL_DISPLAY_BACKEND_MAX7219:
        status = HAL_Display_MAX7219_Fill(color);
        break;
#endif
#if BACKEND_REMOTE_ENABLED
    case HAL_DISPLAY_BACKEND_REMOTE:
        status = HAL_Display_Remote_Fill(color);
        break;
#endif
    default:
        status = HAL_ERR_DEV;
        break;
    }
    display_unlock();
    return status;
}

hal_status_t HAL_Display_Clear(void)
{
    if (!s_initialized) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_CLEAR) != HAL_OK)
        return HAL_ERR_DEV;

    hal_status_t status;
    display_lock();
    switch (s_backend) {
#if BACKEND_ST7796S_ENABLED
    case HAL_DISPLAY_BACKEND_ST7796S:
        status = HAL_Display_ST7796S_Clear();
        break;
#endif
#if BACKEND_MAX7219_ENABLED
    case HAL_DISPLAY_BACKEND_MAX7219:
        status = HAL_Display_MAX7219_Clear();
        break;
#endif
#if BACKEND_REMOTE_ENABLED
    case HAL_DISPLAY_BACKEND_REMOTE:
        status = HAL_Display_Remote_Clear();
        break;
#endif
    default:
        status = HAL_ERR_DEV;
        break;
    }
    display_unlock();
    return status;
}

hal_status_t HAL_Display_Show(void)
{
    if (!s_initialized) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_SHOW) != HAL_OK)
        return HAL_ERR_DEV;

    hal_status_t status;
    display_lock();
    switch (s_backend) {
#if BACKEND_ST7796S_ENABLED
    case HAL_DISPLAY_BACKEND_ST7796S:
        status = HAL_Display_ST7796S_Show();
        break;
#endif
#if BACKEND_MAX7219_ENABLED
    case HAL_DISPLAY_BACKEND_MAX7219:
        status = HAL_Display_MAX7219_Show();
        break;
#endif
#if BACKEND_REMOTE_ENABLED
    case HAL_DISPLAY_BACKEND_REMOTE:
        status = HAL_Display_Remote_Show();
        break;
#endif
    default:
        status = HAL_ERR_DEV;
        break;
    }
    display_unlock();
    return status;
}

hal_status_t HAL_Display_WriteText(const char *text)
{
    if (!s_initialized) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_TEXT) != HAL_OK)
        return HAL_ERR_DEV;

    hal_status_t status;
    display_lock();
    switch (s_backend) {
#if BACKEND_ST7796S_ENABLED
    case HAL_DISPLAY_BACKEND_ST7796S:
        status = HAL_Display_ST7796S_WriteText(text);
        break;
#endif
#if BACKEND_MAX7219_ENABLED
    case HAL_DISPLAY_BACKEND_MAX7219:
        status = HAL_Display_MAX7219_WriteText(text);
        break;
#endif
#if BACKEND_REMOTE_ENABLED
    case HAL_DISPLAY_BACKEND_REMOTE:
        status = HAL_Display_Remote_WriteText(text);
        break;
#endif
    default:
        status = HAL_ERR_DEV;
        break;
    }
    display_unlock();
    return status;
}

hal_status_t HAL_Display_HasCapability(hal_display_cap_t cap)
{
    if (!s_initialized) return HAL_ERR_INIT;
    if (cap >= HAL_CAP_COUNT) return HAL_ERR_DEV;

    hal_status_t status;
    display_lock();
    switch (s_backend) {
#if BACKEND_ST7796S_ENABLED
    case HAL_DISPLAY_BACKEND_ST7796S:
        status = HAL_Display_ST7796S_HasCapability(cap);
        break;
#endif
#if BACKEND_MAX7219_ENABLED
    case HAL_DISPLAY_BACKEND_MAX7219:
        status = HAL_Display_MAX7219_HasCapability(cap);
        break;
#endif
#if BACKEND_REMOTE_ENABLED
    case HAL_DISPLAY_BACKEND_REMOTE:
        status = HAL_Display_Remote_HasCapability(cap);
        break;
#endif
    default:
        status = HAL_ERR_DEV;
        break;
    }
    display_unlock();
    return status;
}

hal_status_t HAL_Display_SetMadctl(uint8_t madctl)
{
    if (!s_initialized) return HAL_ERR_INIT;

    hal_status_t status;
    display_lock();
    switch (s_backend) {
#if BACKEND_ST7796S_ENABLED
    case HAL_DISPLAY_BACKEND_ST7796S:
        status = HAL_Display_ST7796S_SetMadctl(madctl);
        break;
#endif
    default:
        status = HAL_ERR_DEV;
        break;
    }
    display_unlock();
    return status;
}

hal_display_backend_t HAL_Display_GetBackend(void)
{
    return s_backend;
}
