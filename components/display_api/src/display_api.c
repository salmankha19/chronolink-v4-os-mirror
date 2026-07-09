#include "display_api.h"
#include "esp_log.h"

static const char *TAG = "display_api";

int display_init(const DisplayConfig *cfg) {
  (void)cfg;
  ESP_LOGI(TAG, "display_init");
  return 0;
}

int display_blit_region(const uint8_t *buf, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  (void)buf; (void)x; (void)y; (void)w; (void)h;
  return 0;
}

int display_submit_frame_async(uint32_t frame_id) {
  (void)frame_id;
  return 0;
}

int display_set_brightness(uint8_t level) {
  (void)level;
  return 0;
}

void display_register_input_cb(void (*cb)(const void *evt)) {
  (void)cb;
}
