#include "boot_manager.h"
#include "hal.h"
#include "core_os.h"
#include "esp_log.h"

static const char *TAG = "boot_manager";
static boot_stage_t s_last_stage = BOOT_STAGE_NONE;

int boot_manager_init(boot_flags_t flags)
{
    ESP_LOGI(TAG, "Boot manager init (flags=0x%lx)", (unsigned long)flags);
    if (HAL_Init() != HAL_OK) {
        ESP_LOGE(TAG, "HAL_Init failed");
        return -1;
    }
    s_last_stage = BOOT_STAGE_CORE_INIT;
    core_os_init(flags);
    s_last_stage = BOOT_STAGE_CORE_START;
    core_os_start();
    s_last_stage = BOOT_STAGE_HANDOFF;
    ESP_LOGI(TAG, "Boot manager handoff complete");
    return 0;
}

int boot_manager_start(void) { return 0; }

boot_stage_t boot_manager_get_stage(void)
{
    return s_last_stage;
}

void boot_manager_notify_stage(boot_stage_t stage, boot_status_t status)
{
    (void)status;
    s_last_stage = stage;
}
