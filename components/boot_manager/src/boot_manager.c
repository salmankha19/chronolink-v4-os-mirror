#include "boot_manager.h"
#include "hal.h"
#include "core_os.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_vfs_fat.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static const char *TAG = "boot_manager";
static boot_stage_t s_last_stage = BOOT_STAGE_NONE;

/* Wear-levelling handle for the FATFS mount on /storage.
   Kept for the lifetime of the process -- do not unmount. */
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

#define CHRONOLINK_STORAGE_MOUNT_POINT "/storage"
#define CHRONOLINK_STORAGE_PARTITION   "storage"

/* Persistent one-shot boot request (NVS namespace + key).
 * Written by svc_http's POST /api/factory-reset handler, read and cleared
 * here on the next boot. MUST stay in sync with svc_http.c. */
#define NVS_NAMESPACE_BOOT_REQUEST     "boot_req"
#define NVS_KEY_BOOT_REQUEST           "flag"
#define NVS_BOOT_REQUEST_FACTORY_RESET 0x02u  /* == BOOT_FLAG_FACTORY_RESET */

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
 * This file performs no direct hardware access. Hardware is brought up
 * only through HAL_Init(). A factory reset is requested through
 * boot_flags_t (BOOT_FLAG_FACTORY_RESET), never through a button: the
 * BOOT/EN buttons on the PCB are enclosed inside the case and are
 * development/recovery aids only. GPIO0 held low through reset enters
 * the ROM download mode before this code runs.
 * -------------------------------------------------------------------------- */

static void init_logging(void)
{
    /* Keep boot_manager chatter at INFO so the four provisioning lines
       below are visible without enabling debug globally. */
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Log level configured");
}

/* Returns true if a factory reset has been requested for this boot.
   Two producers: a caller-supplied BOOT_FLAG_FACTORY_RESET parameter,
   or a persistent request written by POST /api/factory-reset and read
   from NVS (namespace boot_req, key flag) after init_nvs(). There is
   no hardware trigger. */
static bool factory_reset_requested(boot_flags_t flags)
{
    if (flags & BOOT_FLAG_FACTORY_RESET) {
        ESP_LOGW(TAG, "Factory reset requested via boot flag");
        return true;
    }
    return false;
}

/* Read the one-shot boot request left by svc_http in NVS, if any, and
 * clear it (so a failed reset does not loop). Returns BOOT_FLAG_FACTORY_RESET
 * when the flag was set, 0 otherwise. Called from boot_manager_init() AFTER
 * init_nvs() -- the flag lives in NVS, so it cannot be read earlier.
 *
 * The handle is closed before returning: do_factory_reset() calls
 * nvs_flash_erase(), which requires all NVS handles to be released. */
static boot_flags_t read_pending_boot_request(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_BOOT_REQUEST, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        /* Namespace doesn't exist yet = no request pending. Not an error. */
        return 0;
    }

    uint8_t flag = 0;
    err = nvs_get_u8(h, NVS_KEY_BOOT_REQUEST, &flag);
    if (err != ESP_OK) {
        nvs_close(h);
        return 0;
    }

    /* Clear BEFORE acting: if do_factory_reset() somehow fails to wipe
     * NVS, we still don't want to boot-loop on this flag. */
    nvs_erase_key(h, NVS_KEY_BOOT_REQUEST);
    nvs_commit(h);
    nvs_close(h);

    if (flag & NVS_BOOT_REQUEST_FACTORY_RESET) {
        ESP_LOGW(TAG, "Factory reset requested via NVS (svc_http)");
        return BOOT_FLAG_FACTORY_RESET;
    }
    return 0;
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

    /* Honor a caller-supplied flag first, so a device whose NVS is
     * unreadable can still be reset from the parameter path. */
    if (factory_reset_requested(flags)) {
        do_factory_reset();   /* does not return */
    }

    if (init_nvs() != 0) {
        return -1;
    }

    /* Now that NVS is open, also honor a persistent request written by
     * svc_http's POST /api/factory-reset handler. read_pending_boot_request()
     * clears the flag first, so a reset that fails partway does not loop. */
    if (factory_reset_requested(read_pending_boot_request())) {
        do_factory_reset();   /* does not return */
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