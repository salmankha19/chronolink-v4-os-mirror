#include "ui_state.h"
#include "esp_log.h"

static const char *TAG = "state_manager";

static UIState current = {0, "00:00", 50};

const UIState *state_get_snapshot(void) {
  return &current;
}

void state_publish_update(const UIState *s) {
  (void)s;
  ESP_LOGI(TAG, "state update");
}
