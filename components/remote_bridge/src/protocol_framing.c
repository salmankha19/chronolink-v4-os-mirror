#include "protocol_framing.h"
#include "esp_log.h"

static const char *TAG = "protocol_framing";

void framing_init(void) {
  ESP_LOGI(TAG, "framing init");
}

bool framing_build_and_send(uint8_t type, uint8_t seq, uint16_t trans_id,
                            uint8_t flags, const uint8_t *payload, uint16_t payload_len) {
  (void)type; (void)seq; (void)trans_id; (void)flags; (void)payload; (void)payload_len;
  return true;
}

bool framing_try_parse_frame(uint8_t *out_type, uint8_t *out_seq, uint16_t *out_trans_id,
                             uint8_t *payload_buf, uint16_t buf_capacity, uint16_t *out_payload_len) {
  (void)out_type; (void)out_seq; (void)out_trans_id; (void)payload_buf; (void)buf_capacity; (void)out_payload_len;
  return false;
}
