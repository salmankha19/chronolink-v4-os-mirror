#include "boot_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "core_os.h"
#include "display_api.h"
#include "event_bus.h"
#include "boot_events.h"
#include <string.h>

static boot_flags_t boot_flags = {0};

static const char *TAG = "boot_manager";

bool boot_manager_init(void);
static void detect_safe_mode(void);
static void enter_recovery(void); /* stubbed recovery handler */

/* Helper: publish a boot stage event */
static void publish_boot_stage(boot_stage_t stage, boot_status_t status, int err)
{
    event_t evt;
    boot_event_payload_u p;
    memset(&evt, 0, sizeof(evt));
    memset(&p, 0, sizeof(p));

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

bool boot_manager_init(void)
{
    ESP_LOGI(TAG, "Boot manager start");

    /* NVS stage */
    publish_boot_stage(BOOT_STAGE_NVS, BOOT_STATUS_START, 0);
    if (mount_nvs() != ESP_OK) {
        publish_boot_stage(BOOT_STAGE_NVS, BOOT_STATUS_FAIL, -1);
        publish_boot_fail(BOOT_STAGE_NVS, -1, 0);
        enter_recovery();
        return false;
    }
    publish_boot_stage(BOOT_STAGE_NVS, BOOT_STATUS_OK, 0);

    /* Safe Mode Detection */
    detect_safe_mode();

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
    core_os_init(&boot_flags);
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
    return true;
}

bool cl_fs_mount() {
    return mount_fatfs() == ESP_OK;
}

static void detect_safe_mode(void)
{
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    boot_flags.safe_mode = (gpio_get_level(GPIO_NUM_0) == 0);
    ESP_LOGI(TAG, "Safe mode: %s", boot_flags.safe_mode ? "ON" : "OFF");
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
