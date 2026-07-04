#include "boot_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"

/* Use your actual headers here. These must exist in your project. */
#include "core_os.h"    /* core_os_init(), core_os_start() or kernel_init() */
#include "display_api.h" /* if UI init lives here; otherwise include your ui.h */

static boot_flags_t boot_flags = {0};

static const char *TAG = "boot_manager";

bool boot_manager_init(void);
static void detect_safe_mode(void); 

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

    /* 1. NVS */
    if (mount_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "NVS mount failed");
        return false;
    }

    /* 1.5 Safe Mode Detection */
    detect_safe_mode();

    /* 2. FATFS (non-fatal) */
    if (mount_fatfs() != ESP_OK) {
        ESP_LOGW(TAG, "FATFS mount failed, continuing without filesystem");
        /* If the filesystem is critical for your app, return false here instead */
    }

    /* 3. Core OS init (kernel, state manager, message bus, etc) */
    /* Replace with your kernel_init() if you have that API */
    ESP_LOGI(TAG, "Initializing core OS");
    core_os_init(&boot_flags);

    /* 4. Start core OS (services, tasks, UI) */
    core_os_start();

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
