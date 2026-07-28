#include "boot_manager.h"
#include "hal.h"
#include "core_os.h"
#include "esp_log.h"

static const char *TAG = "boot_manager";

int boot_manager_init(uint32_t flags)
{
    ESP_LOGI(TAG, "Boot manager init (flags=0x%lx)", (unsigned long)flags);
    if (HAL_Init() != HAL_OK) {
        ESP_LOGE(TAG, "HAL_Init failed");
        return -1;
    }
    core_os_init((boot_flags_t)flags);
    core_os_start();
    ESP_LOGI(TAG, "Boot manager handoff complete");
    return 0;
}

int boot_manager_start(void) { return 0; }
