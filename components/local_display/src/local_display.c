#include "local_display.h"
#include "esp_log.h"

static const char *TAG = "local_display";

int local_display_init(const DisplayConfig *cfg) {
  (void)cfg;
  ESP_LOGI(TAG, "local_display init");
  return 0;
}
