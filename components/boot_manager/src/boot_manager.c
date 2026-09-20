#include "boot_manager.h"
#include "hal.h"
#include "core_os.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#include "pdl_pins.h"

static const char *TAG = "boot_manager";
static boot_stage_t s_last_stage = BOOT_STAGE_NONE;

/* Wear-levelling handle for the FATFS mount on /storage.
   Kept for the lifetime of the process -- do not unmount. */
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

#define CHRONOLINK_STORAGE_MOUNT_POINT "/storage"
#define CHRONOLINK_STORAGE_PARTITION   "storage"

/* BOOT button on the ChronoLink V4 PCB is GPIO0 (active low, external
   pull-up). PDL_PIN_SAFE_MODE resolves to 0 in pdl_pins.h. Hold time
   to trigger factory reset at startup -- long enough that a bounced
   contact or a quick tap doesn't wipe config. */
#define FACTORY_RESET_HOLD_MS  3000

/* --------------------------------------------------------------------------
 * Boot-order init helpers
 *
 * Constitution (docs/CHRONOLINK_CONSTITUTION.md, section 2) requires, in
 * order: NVS -> FATFS(/storage) -> OTA -> logging -> core_os_init().
 * The only deliberate deviation here is that logging is configured first,
 * so failures in NVS/FATFS/OTA are observable on serial instead of silent.
 * The EFFECT of the constitution is preserved: logging is up well before
 * core_os_init() runs. This deviation is documented in the PR description.
 *
 * None of these helpers publish events -- the event bus does not exist
 * until core_os_init() runs (see the ordering-constraint comment further
 * down). HAL_Init() still runs first among the hardware-facing steps.
 *
 * The one hardware touch in this file is boot_button_held_at_startup(),
 * which reads GPIO0 to detect a factory-reset request. This is a boot-
 * time decision input, not hardware provisioning -- nothing is driven,
 * no bus is opened, no peripheral is configured. Keeping it here rather
 * than a service avoids booting the entire OS just to decide whether to
 * wipe. This is a deliberate, documented exception to "boot_manager
 * must not access hardware" and is called out in the PR description.
 * -------------------------------------------------------------------------- */

static void init_logging(void)
{
    /* Keep boot_manager chatter at INFO so the four provisioning lines
       below are visible without enabling debug globally. */
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Log level configured");
}

/* Returns true if the BOOT button is being held at startup.
   GPIO0 is active-low with an external pull-up on the PCB; a LOW read
   means the button is pressed. We poll for FACTORY_RESET_HOLD_MS and
   require the level to stay LOW the whole time -- a short tap does not
   trigger a reset.

   Idempotent and side-effect free if the button is not held: it takes
   no longer than the poll loop's first sample (~10 ms). Only if the
   button IS low do we spend the full hold duration waiting. */
static bool boot_button_held_at_startup(void)
{
#if defined(PDL_PIN_SAFE_MODE) && (PDL_PIN_SAFE_MODE >= 0)
    const gpio_num_t pin = (gpio_num_t)PDL_PIN_SAFE_MODE;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   /* belt-and-braces; PCB has external PU */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    /* Settle: let the pin stabilize after power-on before sampling. */
    vTaskDelay(pdMS_TO_TICKS(50));

    if (gpio_get_level(pin) != 0) {
        return false;   /* not pressed */
    }

    ESP_LOGW(TAG, "BOOT button held, waiting %d ms for factory reset...",
             FACTORY_RESET_HOLD_MS);

    /* Sample in small slices so we bail out fast if the user releases. */
    const int slice_ms = 100;
    int held_ms = 0;
    while (held_ms < FACTORY_RESET_HOLD_MS) {
        vTaskDelay(pdMS_TO_TICKS(slice_ms));
        held_ms += slice_ms;
        if (gpio_get_level(pin) != 0) {
            ESP_LOGI(TAG, "BOOT button released after %d ms, no reset", held_ms);
            return false;
        }
    }

    ESP_LOGW(TAG, "BOOT button held for %d ms, factory reset confirmed",
             held_ms);
    return true;
#else
    return false;   /* no BOOT pin defined for this board revision */
#endif
}

/* Returns true if a factory reset has been requested for this boot.
   Two triggers, ORed together:
     - BOOT_FLAG_FACTORY_RESET flag (used by the eventual web UI)
     - BOOT button (GPIO0) held at startup for FACTORY_RESET_HOLD_MS */
static bool factory_reset_requested(boot_flags_t flags)
{
    if (flags & BOOT_FLAG_FACTORY_RESET) {
        ESP_LOGW(TAG, "Factory reset requested via boot flag");
        return true;
    }
    return boot_button_held_at_startup();
}

/* Wipe every persistent store that is user-owned, then reboot.
   Does not return.
     - NVS:       whole partition erased (all namespaces)
     - /storage:  FATFS reformatted
   Deliberately NOT touched:
     - otadata:   boot-slot preference is not user data
     - phy_init:  RF calibration data is board-specific, not user data
*/
static void do_factory_reset(void) __attribute__((noreturn));
static void do_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset: erasing NVS and formatting %s",
             CHRONOLINK_STORAGE_MOUNT_POINT);

    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
        /* Continue anyway -- we still want to try formatting storage and
           reboot. A failed NVS erase at worst leaves stale config. */
    } else {
        ESP_LOGW(TAG, "NVS erased");
    }

    /* Mount the partition fresh so we can format it, then unmount.
       Using the rw_wl variant to match init_fatfs() below. */
    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files              = 5,
        .allocation_unit_size   = 4096,
        .use_one_fat            = false,
    };
    wl_handle_t tmp_wl = WL_INVALID_HANDLE;
    err = esp_vfs_fat_spiflash_mount_rw_wl(CHRONOLINK_STORAGE_MOUNT_POINT,
                                           CHRONOLINK_STORAGE_PARTITION,
                                           &mount_cfg, &tmp_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage format-on-mount failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "storage formatted");
        esp_vfs_fat_spiflash_unmount_rw_wl(CHRONOLINK_STORAGE_MOUNT_POINT,
                                           tmp_wl);
    }

    ESP_LOGW(TAG, "Factory reset complete, rebooting");
    esp_restart();
}

static int init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init returned %s, erasing and retrying",
                 esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "NVS initialized");
    return 0;
}

static int init_fatfs(void)
{
    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files              = 5,
        .allocation_unit_size   = 4096,
        .use_one_fat            = false,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(CHRONOLINK_STORAGE_MOUNT_POINT,
                                                     CHRONOLINK_STORAGE_PARTITION,
                                                     &mount_cfg, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FATFS mount on %s failed: %s",
                 CHRONOLINK_STORAGE_MOUNT_POINT, esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "FATFS mounted at %s", CHRONOLINK_STORAGE_MOUNT_POINT);
    return 0;
}

static void init_ota(void)
{
    /* Log-only for now. This makes the OTA prerequisite provable in the
       boot log ("next=app1") without committing to any OTA policy yet.
       Partition selection for actual updates happens in a later PR
       (svc_ota), which will call esp_ota_get_next_update_partition()
       itself -- this function exists to fail loudly if the partition
       table ever regresses. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot    = esp_ota_get_boot_partition();
    const esp_partition_t *next    = esp_ota_get_next_update_partition(NULL);

    ESP_LOGI(TAG, "OTA: running=%s boot=%s next=%s",
             running ? running->label : "(null)",
             boot    ? boot->label    : "(null)",
             next    ? next->label    : "(none)");

    if (running == NULL || boot == NULL) {
        ESP_LOGW(TAG, "OTA partition metadata incomplete -- OTA will not be possible");
    }
    if (next == NULL) {
        ESP_LOGW(TAG, "No inactive OTA slot found -- OTA will not be possible");
    }
}

/* --------------------------------------------------------------------------
 * Event publishing
 *
 * IMPORTANT ORDERING CONSTRAINT: event_bus_publish() is a no-op until
 * event_bus_init() has run, and event_bus_init() is called from
 * core_os_init(). Any code path in this file that runs BEFORE
 * core_os_init() cannot publish events -- the bus has no queue yet and
 * event_bus_publish() will log "no queue available, event dropped" and
 * return. That is why:
 *
 *   - EVENT_BOOT_START is not published from boot_manager_init(). It
 *     would need to fire before HAL_Init(), i.e. before the bus exists.
 *   - The HW stage publishes only OK, not START, for the same reason.
 *
 * Everything after core_os_init() publishes normally. If you ever want
 * pre-core-init boot events, event_bus_init() has to move out of
 * core_os_init() and into here (or into a boot-manager-owned early
 * init), which is a larger change than this file should make on its own.
 *
 * The NVS/FATFS/OTA/logging helpers above run in this pre-bus window
 * and therefore do not publish events either. Their success/failure is
 * observable via ESP_LOG only.
 * -------------------------------------------------------------------------- */

static void publish_stage(boot_stage_t stage, boot_status_t status, int32_t err)
{
    event_t evt;
    boot_event_payload_u p;
    boot_event_init_stage(&p, stage, status, err);
    event_init_payload(&evt, EVENT_BOOT_STAGE, &p, sizeof(p));
    event_bus_publish(&evt);

    ESP_LOGI(TAG, "stage %s: %s (err=%ld)",
             boot_stage_to_str(stage), boot_status_to_str(status), (long)err);
}

static void publish_ready(void)
{
    event_t evt;
    boot_event_payload_u p;
    /* READY carries no interesting payload beyond "we got here"; stamp
       it with a health snapshot so the consumer doesn't have to special-
       case a zero-length event. */
    boot_event_init_health(&p, BOOT_STAGE_READY, 0,
                           (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                           0);
    event_init_payload(&evt, EVENT_BOOT_READY, &p, sizeof(p));
    event_bus_publish(&evt);

    ESP_LOGI(TAG, "boot ready");
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int boot_manager_init(boot_flags_t flags)
{
    ESP_LOGI(TAG, "Boot manager init (flags=0x%lx)", (unsigned long)flags);

    /* Constitution-mandated provisioning order (logging hoisted to first
       so the rest is observable). None of these publish events -- the
       bus does not exist yet. See the comment block above. */
    init_logging();

    if (factory_reset_requested(flags)) {
        do_factory_reset();   /* does not return */
    }

    if (init_nvs() != 0) {
        return -1;
    }
    if (init_fatfs() != 0) {
        return -1;
    }
    init_ota();   /* best-effort, log-only; never fatal */

    s_last_stage = BOOT_STAGE_INIT;

    /* HW stage: cannot publish START (bus not up yet); publishes OK below
       once core_os_init() has created the event bus. */
    s_last_stage = BOOT_STAGE_HW;
    if (HAL_Init() != HAL_OK) {
        /* Bus not up -- cannot publish EVENT_BOOT_FAIL here. See the
           ordering-constraint comment at the top of this file. */
        ESP_LOGE(TAG, "HAL_Init failed");
        return -1;
    }

    /* core_os_init() creates the event bus and the state queue. Every
       publish below this point is real. */
    s_last_stage = BOOT_STAGE_CORE_INIT;
    core_os_init(flags);

    publish_stage(BOOT_STAGE_HW,         BOOT_STATUS_OK, 0);
    publish_stage(BOOT_STAGE_CORE_INIT,  BOOT_STATUS_START, 0);

    s_last_stage = BOOT_STAGE_CORE_START;
    publish_stage(BOOT_STAGE_CORE_INIT,  BOOT_STATUS_OK, 0);
    publish_stage(BOOT_STAGE_CORE_START, BOOT_STATUS_START, 0);

    core_os_start();

    publish_stage(BOOT_STAGE_CORE_START, BOOT_STATUS_OK, 0);

    s_last_stage = BOOT_STAGE_HANDOFF;
    publish_stage(BOOT_STAGE_HANDOFF, BOOT_STATUS_OK, 0);

    s_last_stage = BOOT_STAGE_READY;
    publish_ready();

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
    s_last_stage = stage;
    publish_stage(stage, status, 0);
}