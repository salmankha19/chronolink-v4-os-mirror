#include "boot_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "core_os.h"
#include "display_api.h"
#include "event_bus.h"
#include "boot_events.h"
#include "driver/gpio.h"
#include <string.h>

/* HAL includes used by boot manager for safe-mode and HAL init */
#include "hal.h"
#include "hal_gpio.h"
#include "pdl_pins.h"

static boot_flags_t boot_flags = 0;
static boot_stage_t g_last_stage = BOOT_STAGE_NONE;

static const char *TAG = "boot_manager";

int boot_manager_init(boot_flags_t flags);
static void detect_safe_mode(void);
static void enter_recovery(void); /* stubbed recovery handler */

/* GPIO2 is boot-sensitive on many ESP32 designs; drive it high early to avoid conflicts. */
static void force_gpio2_safe_state(void)
{
    gpio_config_t io;
    memset(&io, 0, sizeof(io));
    io.pin_bit_mask = (1ULL << 2);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE("HWINIT", "GPIO2 safe-state config failed: %s", esp_err_to_name(err));
        return;
    }

    err = gpio_set_level(GPIO_NUM_2, 1);
    if (err != ESP_OK) {
        ESP_LOGE("HWINIT", "GPIO2 safe-state set failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI("HWINIT", "GPIO2 forced HIGH early (boot-sensitive pin)");
}

/* Helper: publish a boot stage event */
static void publish_boot_stage(boot_stage_t stage, boot_status_t status, int err)
{
    event_t evt;
    boot_event_payload_u p;
    memset(&evt, 0, sizeof(evt));
    memset(&p, 0, sizeof(p));

    /* cache last stage for external queries */
    g_last_stage = stage;

    p.stage.stage = (uint8_t)stage;
    p.stage.status = (uint8_t)status;
    p.stage.err = err;

    /* Use the event_bus helper to initialize payload safely */
    event_init_payload(&evt, EVENT_BOOT_STAGE, &p, sizeof(p));
    event_bus_publish(&evt);

    ESP_LOGI(TAG, "BOOT_STAGE %s %s (err=%d)",
             boot_stage_to_str(stage),
             boot_status_to_str(status),
             err);
}

/* Helper: publish a boot fail event */
static void publish_boot_fail(boot_stage_t stage, int err, uint32_t flags)
{
    event_t evt;
    boot_event_payload_u p;
    memset(&evt, 0, sizeof(evt));
    memset(&p, 0, sizeof(p));

    p.fail.stage = (uint8_t)stage;
    p.fail.err = err;
    p.fail.fail_flags = flags;

    event_init_payload(&evt, EVENT_BOOT_FAIL, &p, sizeof(p));
    event_bus_publish(&evt);

    ESP_LOGE(TAG, "BOOT_FAIL %s err=%d flags=0x%08x",
             boot_stage_to_str(stage),
             err,
             flags);
}

static esp_err_t mount_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing and retrying");
        esp_err_t e2 = nvs_flash_erase();
        if (e2 != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(e2));
            return e2;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "NVS initialized");
    }
    return err;
}

static esp_err_t mount_fatfs(void)
{
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 4096,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    wl_handle_t wl_handle;
    esp_err_t err = esp_vfs_fat_spiflash_mount("/storage", "storage", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_vfs_fat_spiflash_mount failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "FATFS mounted at /storage from partition label 'storage'");
    return ESP_OK;
}

int boot_manager_init(boot_flags_t flags)
{
    boot_flags = flags;
    ESP_LOGI(TAG, "Boot manager start");

    force_gpio2_safe_state();

    /* NVS stage */
    publish_boot_stage(BOOT_STAGE_NVS, BOOT_STATUS_START, 0);
    if (mount_nvs() != ESP_OK) {
        publish_boot_stage(BOOT_STAGE_NVS, BOOT_STATUS_FAIL, -1);
        publish_boot_fail(BOOT_STAGE_NVS, -1, 0);
        enter_recovery();
        return -1;
    }
    publish_boot_stage(BOOT_STAGE_NVS, BOOT_STATUS_OK, 0);

    /* Safe Mode Detection */
    detect_safe_mode();

    /* HAL init stage */
    publish_boot_stage(BOOT_STAGE_HAL_INIT, BOOT_STATUS_START, 0);
    if (HAL_Init() != HAL_OK) {
        publish_boot_stage(BOOT_STAGE_HAL_INIT, BOOT_STATUS_FAIL, -3);
        publish_boot_fail(BOOT_STAGE_HAL_INIT, -3, 0);
        enter_recovery();
        return -1;
    }
    publish_boot_stage(BOOT_STAGE_HAL_INIT, BOOT_STATUS_OK, 0);

    /* FATFS stage (non-fatal) */
    publish_boot_stage(BOOT_STAGE_FATFS, BOOT_STATUS_START, 0);
    if (mount_fatfs() != ESP_OK) {
        publish_boot_stage(BOOT_STAGE_FATFS, BOOT_STATUS_FAIL, -2);
        ESP_LOGW(TAG, "FATFS mount failed, continuing without filesystem");
        /* publish a non-fatal fail event so telemetry can record it */
        publish_boot_fail(BOOT_STAGE_FATFS, -2, 0);
    } else {
        publish_boot_stage(BOOT_STAGE_FATFS, BOOT_STATUS_OK, 0);
    }

    /* CORE_INIT stage */
    publish_boot_stage(BOOT_STAGE_CORE_INIT, BOOT_STATUS_START, 0);
    ESP_LOGI(TAG, "Initializing core OS");
    core_os_init(boot_flags);
    publish_boot_stage(BOOT_STAGE_CORE_INIT, BOOT_STATUS_OK, 0);

    /* CORE_START stage */
    publish_boot_stage(BOOT_STAGE_CORE_START, BOOT_STATUS_START, 0);
    core_os_start();
    publish_boot_stage(BOOT_STAGE_CORE_START, BOOT_STATUS_OK, 0);

    /* HANDOFF stage */
    publish_boot_stage(BOOT_STAGE_HANDOFF, BOOT_STATUS_START, 0);
    /* If you have a kernel handoff or init process, do it here. For now we mark handoff OK. */
    publish_boot_stage(BOOT_STAGE_HANDOFF, BOOT_STATUS_OK, 0);

    ESP_LOGI(TAG, "Boot manager finished successfully");
    return 0;
}

bool cl_fs_mount() {
    return mount_fatfs() == ESP_OK;
}

static void detect_safe_mode(void)
{
    /* Use HAL GPIO abstraction and PDL pin mapping */
    HAL_GPIO_Init(); /* ensure GPIO subsystem configured */
    if (HAL_GPIO_Read(PDL_PIN_SAFE_MODE) == GPIO_LOW) {
        boot_flags |= BOOT_FLAG_SAFE_MODE;
    } else {
        boot_flags &= ~BOOT_FLAG_SAFE_MODE;
    }
    ESP_LOGI(TAG, "Safe mode: %s", (boot_flags & BOOT_FLAG_SAFE_MODE) ? "ON" : "OFF");
}

/* Minimal recovery stub: increment persistent failure counter, set state, attempt fallback */
static void enter_recovery(void)
{
    ESP_LOGW(TAG, "Entering recovery mode (stub). Implement rollback/factory reset here.");
    /* Publish RECOVERY stage event */
    publish_boot_stage(BOOT_STAGE_RECOVERY, BOOT_STATUS_START, 0);
    /* TODO: implement persistent failure counter in NVS and actual recovery actions */
    publish_boot_stage(BOOT_STAGE_RECOVERY, BOOT_STATUS_OK, 0);
}

/* Public accessors */
boot_stage_t boot_manager_get_stage(void)
{
    return g_last_stage;
}

void boot_manager_notify_stage(boot_stage_t stage, boot_status_t status)
{
    publish_boot_stage(stage, status, 0);
}
