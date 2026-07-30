#include "display_manager.h"

#include "esp_log.h"
#include "json_display_executor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"

#include <string.h>

typedef struct {
    char json[JSON_EXEC_MAX_LEN];
} display_frame_t;

static const char *TAG = "display_mgr";

static QueueHandle_t s_queue = NULL;
static display_manager_stats_t s_stats = {0};

static bool in_isr_context(void)
{
    return xPortInIsrContext();
}

void display_manager_init(void)
{
    if (s_queue != NULL) {
        return;
    }

    s_queue = xQueueCreate(8, sizeof(display_frame_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create display queue");
    }
}

bool display_manager_submit_json(const char *json, size_t len)
{
    if ((json == NULL) || (len == 0U) || (len >= JSON_EXEC_MAX_LEN) || (s_queue == NULL)) {
        s_stats.frames_dropped++;
        return false;
    }

    display_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    memcpy(frame.json, json, len);
    frame.json[len] = '\0';

    BaseType_t sent = pdFALSE;

    if (in_isr_context()) {
        BaseType_t higher_prio_woken = pdFALSE;
        sent = xQueueSendFromISR(s_queue, &frame, &higher_prio_woken);
        if (higher_prio_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        sent = xQueueSend(s_queue, &frame, 0);
    }

    if (sent != pdTRUE) {
        s_stats.frames_dropped++;
        return false;
    }

    return true;
}

void display_manager_process_frame(void)
{
    if (s_queue == NULL) {
        return;
    }

    display_frame_t frame;
    if (xQueueReceive(s_queue, &frame, 0) != pdTRUE) {
        return;
    }

    size_t frame_len = strnlen(frame.json, JSON_EXEC_MAX_LEN);
    char err_buf[64] = {0};
    if (json_execute(frame.json, frame_len, err_buf, sizeof(err_buf)) == 0) {
        s_stats.frames_processed++;
    } else {
        s_stats.parse_errors++;
        ESP_LOGW(TAG, "json_execute failed: %s", err_buf[0] ? err_buf : "unknown");
    }
}

display_manager_stats_t display_manager_get_stats(void)
{
    return s_stats;
}
