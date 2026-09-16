/*
 * hal_display.c
 *
 * ChronoLink V4 OS — Hardware Abstraction Layer
 * Display Router / Backend Dispatcher
 *
 * Responsibilities:
 *  - Compile-time backend selection (Kconfig)
 *  - Thread-safe dispatch to the active backend via FreeRTOS mutex
 *  - State tracking (init, backend type)
 *  - Capability routing
 *
 * Dispatch model:
 *  Each backend provides a single display_backend_ops_t struct whose
 *  fields are function pointers. The router selects one ops table at
 *  Init() time and calls through s_ops.
 *
 * Adding a new backend:
 *    1. Implement the backend's functions in hal_display_<name>.c
 *    2. Export `const display_backend_ops_t HAL_DISPLAY_<NAME>_OPS = {...}`
 *    3. Add a #include and one line in hal_display_select_ops() below
 *  No other file needs to change.
 *
 * Backends are included conditionally so unused drivers are not linked.
 */

#include "sdkconfig.h"
#include "hal_display.h"
#include "esp_log.h"

#if CONFIG_CHRONOLINK_HAL_DISPLAY_MUTEX
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

/* --------------------------------------------------------------------------
 * Conditionally include backend headers
 * -------------------------------------------------------------------------- */
#if defined(CONFIG_CHRONOLINK_HAL_DISPLAY_BACKEND_ST7796S)
#define BACKEND_ST7796S_ENABLED 1
#include "hal_display_st7796s.h"
#else
#define BACKEND_ST7796S_ENABLED 0
#endif

#if defined(CONFIG_CHRONOLINK_HAL_DISPLAY_BACKEND_MAX7219)
#define BACKEND_MAX7219_ENABLED 1
#include "hal_display_max7219.h"
#else
#define BACKEND_MAX7219_ENABLED 0
#endif

#if defined(CONFIG_CHRONOLINK_HAL_DISPLAY_BACKEND_REMOTE)
#define BACKEND_REMOTE_ENABLED 1
#include "hal_display_remote.h"
#else
#define BACKEND_REMOTE_ENABLED 0
#endif

#include "chronolink_driver_sdk.h"
#define BACKEND_CUSTOM_ENABLED 1

#if defined(CONFIG_CHRONOLINK_USE_MOCK_DISPLAY)
#define BACKEND_MOCK_ENABLED 1
#include "hal_display_mock.h"
#else
#define BACKEND_MOCK_ENABLED 0
#endif

static const char *TAG = "HAL_DISPLAY";

/* --------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------- */
static hal_display_backend_t s_backend = HAL_DISPLAY_BACKEND_NONE;
static const display_backend_ops_t *s_ops = NULL;
static bool s_initialized = false;

/* Thread-safety: display is a shared resource; SPI polling is not reentrant */
#if CONFIG_CHRONOLINK_HAL_DISPLAY_MUTEX
static SemaphoreHandle_t s_mutex = NULL;
#endif

/* --------------------------------------------------------------------------
 * Lock helpers
 * -------------------------------------------------------------------------- */
static inline bool display_lock(void)
{
#if CONFIG_CHRONOLINK_HAL_DISPLAY_MUTEX
    if (!s_mutex) return true;
    return (xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY) == pdTRUE);
#else
    return true;
#endif
}

static inline void display_unlock(void)
{
#if CONFIG_CHRONOLINK_HAL_DISPLAY_MUTEX
    if (s_mutex) xSemaphoreGiveRecursive(s_mutex);
#endif
}

/* --------------------------------------------------------------------------
 * Custom driver adapter
 *
 * chronolink_display_driver_t has the same field names as
 * display_backend_ops_t. Forward each call to the currently-registered
 * custom driver. If no driver is registered, every method returns
 * HAL_ERR_DEV (except deinit, which returns HAL_OK).
 * -------------------------------------------------------------------------- */
#if BACKEND_CUSTOM_ENABLED
static inline const chronolink_display_driver_t *custom_drv(void)
{
    return chronolink_driver_get();
}

static hal_status_t custom_init(void)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->init) ? d->init() : HAL_ERR_INIT;
}

static hal_status_t custom_deinit(void)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->deinit) ? d->deinit() : HAL_OK;
}

static hal_status_t custom_draw_pixel(uint16_t x, uint16_t y, uint32_t c)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->draw_pixel) ? d->draw_pixel(x, y, c) : HAL_ERR_DEV;
}

static hal_status_t custom_fill(uint32_t c)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->fill) ? d->fill(c) : HAL_ERR_DEV;
}

static hal_status_t custom_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t c)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->fill_rect) ? d->fill_rect(x, y, w, h, c) : HAL_ERR_DEV;
}

static hal_status_t custom_blit_row(uint16_t x, uint16_t y, const uint32_t *p, uint16_t len)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->blit_row) ? d->blit_row(x, y, p, len) : HAL_ERR_DEV;
}

static hal_status_t custom_clear(void)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->clear) ? d->clear() : HAL_ERR_DEV;
}

static hal_status_t custom_show(void)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->show) ? d->show() : HAL_ERR_DEV;
}

static hal_status_t custom_write_text(const char *t)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->write_text) ? d->write_text(t) : HAL_ERR_DEV;
}

static hal_status_t custom_has_capability(hal_display_cap_t cap)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->has_capability) ? d->has_capability(cap) : HAL_ERR_DEV;
}

static hal_status_t custom_set_madctl(uint8_t m)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->set_madctl) ? d->set_madctl(m) : HAL_ERR_DEV;
}

static hal_status_t custom_set_scroll_area(uint16_t tfa, uint16_t vsa, uint16_t bfa)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->set_scroll_area) ? d->set_scroll_area(tfa, vsa, bfa) : HAL_ERR_DEV;
}

static hal_status_t custom_set_scroll_start(uint16_t vss)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->set_scroll_start) ? d->set_scroll_start(vss) : HAL_ERR_DEV;
}

static int custom_get_width(void)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->get_width) ? d->get_width() : 0;
}

static int custom_get_height(void)
{
    const chronolink_display_driver_t *d = custom_drv();
    return (d && d->get_height) ? d->get_height() : 0;
}

static const display_backend_ops_t s_custom_ops = {
    .name             = "custom",
    .init             = custom_init,
    .deinit           = custom_deinit,
    .draw_pixel       = custom_draw_pixel,
    .fill             = custom_fill,
    .fill_rect        = custom_fill_rect,
    .blit_row         = custom_blit_row,
    .clear            = custom_clear,
    .show             = custom_show,
    .write_text       = custom_write_text,
    .has_capability   = custom_has_capability,
    .set_madctl       = custom_set_madctl,
    .set_scroll_area  = custom_set_scroll_area,
    .set_scroll_start = custom_set_scroll_start,
    .get_width        = custom_get_width,
    .get_height       = custom_get_height,
};
#endif /* BACKEND_CUSTOM_ENABLED */

/* --------------------------------------------------------------------------
 * Backend ops selection (compile-time via Kconfig)
 *
 * Priority order (unchanged from the previous implementation):
 *   MOCK > ST7796S > MAX7219 > REMOTE > CUSTOM
 * -------------------------------------------------------------------------- */
static const display_backend_ops_t *hal_display_select_ops(void)
{
#if BACKEND_MOCK_ENABLED
    return &HAL_DISPLAY_MOCK_OPS;
#elif BACKEND_ST7796S_ENABLED
    return &HAL_DISPLAY_ST7796S_OPS;
#elif BACKEND_MAX7219_ENABLED
    return &HAL_DISPLAY_MAX7219_OPS;
#elif BACKEND_REMOTE_ENABLED
    return &HAL_DISPLAY_REMOTE_OPS;
#elif BACKEND_CUSTOM_ENABLED
    return chronolink_driver_is_registered() ? &s_custom_ops : NULL;
#else
    return NULL;
#endif
}

/* Map an ops pointer back to the public backend enum (for GetBackend()). */
static hal_display_backend_t hal_display_ops_to_backend(const display_backend_ops_t *ops)
{
    if (ops == NULL) return HAL_DISPLAY_BACKEND_NONE;

#if BACKEND_MOCK_ENABLED
    if (ops == &HAL_DISPLAY_MOCK_OPS) return HAL_DISPLAY_BACKEND_MOCK;
#endif
#if BACKEND_ST7796S_ENABLED
    if (ops == &HAL_DISPLAY_ST7796S_OPS) return HAL_DISPLAY_BACKEND_ST7796S;
#endif
#if BACKEND_MAX7219_ENABLED
    if (ops == &HAL_DISPLAY_MAX7219_OPS) return HAL_DISPLAY_BACKEND_MAX7219;
#endif
#if BACKEND_REMOTE_ENABLED
    if (ops == &HAL_DISPLAY_REMOTE_OPS) return HAL_DISPLAY_BACKEND_REMOTE;
#endif
#if BACKEND_CUSTOM_ENABLED
    if (ops == &s_custom_ops) return HAL_DISPLAY_BACKEND_CUSTOM;
#endif

    return HAL_DISPLAY_BACKEND_NONE;
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

    s_ops = hal_display_select_ops();
    s_backend = hal_display_ops_to_backend(s_ops);

    if (s_ops == NULL || s_backend == HAL_DISPLAY_BACKEND_NONE) {
        ESP_LOGW(TAG, "No display backend selected (headless mode)");
        /* Soft-fail: system can run headless */
        return HAL_OK;
    }

    /* Create mutex first (do not mark initialized if mutex creation fails) */
#if CONFIG_CHRONOLINK_HAL_DISPLAY_MUTEX
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateRecursiveMutex();
        if (!s_mutex) {
            ESP_LOGE(TAG, "Failed to create display mutex");
            s_ops = NULL;
            s_backend = HAL_DISPLAY_BACKEND_NONE;
            return HAL_ERR_INIT;
        }
    }
#endif

    if (!display_lock()) {
        ESP_LOGE(TAG, "Failed to take display mutex during init");
        s_ops = NULL;
        s_backend = HAL_DISPLAY_BACKEND_NONE;
        return HAL_ERR_INIT;
    }

    ESP_LOGI(TAG, "Initializing %s display backend",
             s_ops->name ? s_ops->name : "?");

    hal_status_t status = s_ops->init ? s_ops->init() : HAL_ERR_INIT;

    display_unlock();

    if (status == HAL_OK) {
        s_initialized = true;
        ESP_LOGI(TAG, "Display initialized (backend=%d, name=%s)",
                 (int)s_backend, s_ops->name ? s_ops->name : "?");
    } else {
        ESP_LOGE(TAG, "Backend init failed: %d", (int)status);
        s_ops = NULL;
        s_backend = HAL_DISPLAY_BACKEND_NONE;
    }

    return status;
}

hal_status_t HAL_Display_Deinit(void)
{
    if (!s_initialized) {
        return HAL_OK;
    }

    hal_status_t status = HAL_OK;

    if (!display_lock()) return HAL_ERR_INIT;

    if (s_ops && s_ops->deinit) {
        status = s_ops->deinit();
    }

    display_unlock();

    s_initialized = false;
    s_backend = HAL_DISPLAY_BACKEND_NONE;
    s_ops = NULL;

    ESP_LOGI(TAG, "Display deinitialized");
    return status;
}

hal_status_t HAL_Display_DrawPixel(uint16_t x, uint16_t y, uint32_t color)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_DRAW_PIXEL) != HAL_OK) return HAL_ERR_DEV;
    if (!s_ops->draw_pixel) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->draw_pixel(x, y, color);
    display_unlock();
    return status;
}

hal_status_t HAL_Display_Fill(uint32_t color)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_FILL) != HAL_OK) return HAL_ERR_DEV;
    if (!s_ops->fill) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->fill(color);
    display_unlock();
    return status;
}

hal_status_t HAL_Display_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (w == 0 || h == 0) return HAL_OK;
    if (!s_ops->fill_rect) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->fill_rect(x, y, w, h, color);
    display_unlock();
    return status;
}

hal_status_t HAL_Display_BlitRow(uint16_t x, uint16_t y, const uint32_t *pixels24, uint16_t len)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (len == 0) return HAL_OK;
    if (!pixels24) return HAL_ERR_DEV;
    if (!s_ops->blit_row) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->blit_row(x, y, pixels24, len);
    display_unlock();
    return status;
}

hal_status_t HAL_Display_Clear(void)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_CLEAR) != HAL_OK) return HAL_ERR_DEV;
    if (!s_ops->clear) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->clear();
    display_unlock();
    return status;
}

hal_status_t HAL_Display_Show(void)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_SHOW) != HAL_OK) return HAL_ERR_DEV;
    if (!s_ops->show) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->show();
    display_unlock();
    return status;
}

hal_status_t HAL_Display_WriteText(const char *text)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_TEXT) != HAL_OK) return HAL_ERR_DEV;
    if (!s_ops->write_text) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->write_text(text);
    display_unlock();
    return status;
}

hal_status_t HAL_Display_HasCapability(hal_display_cap_t cap)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (cap >= HAL_CAP_COUNT) return HAL_ERR_DEV;

    /* NOTE: no lock here. HasCapability is a pure metadata query with no
       side effects, and it is called from inside other HAL_Display_*
       functions that already hold the lock. Taking the recursive mutex
       again would be safe but unnecessary. */
    if (!s_ops->has_capability) return HAL_ERR_DEV;
    return s_ops->has_capability(cap);
}

hal_status_t HAL_Display_SetMadctl(uint8_t madctl)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_ORIENTATION) != HAL_OK) return HAL_ERR_DEV;
    if (!s_ops->set_madctl) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->set_madctl(madctl);
    display_unlock();
    return status;
}

int HAL_Display_GetWidth(void)
{
    if (!s_initialized || !s_ops || !s_ops->get_width) return 0;
    return s_ops->get_width();
}

int HAL_Display_GetHeight(void)
{
    if (!s_initialized || !s_ops || !s_ops->get_height) return 0;
    return s_ops->get_height();
}

hal_display_backend_t HAL_Display_GetBackend(void)
{
    return s_backend;
}

hal_status_t HAL_Display_SetScrollArea(uint16_t tfa, uint16_t vsa, uint16_t bfa)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_SCROLL) != HAL_OK) return HAL_ERR_DEV;
    if (!s_ops->set_scroll_area) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->set_scroll_area(tfa, vsa, bfa);
    display_unlock();
    return status;
}

hal_status_t HAL_Display_SetScrollStart(uint16_t vss)
{
    if (!s_initialized || !s_ops) return HAL_ERR_INIT;
    if (HAL_Display_HasCapability(HAL_CAP_SCROLL) != HAL_OK) return HAL_ERR_DEV;
    if (!s_ops->set_scroll_start) return HAL_ERR_DEV;

    if (!display_lock()) return HAL_ERR_DEV;
    hal_status_t status = s_ops->set_scroll_start(vss);
    display_unlock();
    return status;
}