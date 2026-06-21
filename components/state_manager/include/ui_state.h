#pragma once
#include <stdint.h>
typedef struct {
  uint32_t unix_ts;
  char time_str[16];
  uint8_t brightness;
} UIState;
const UIState *state_get_snapshot(void);
void state_publish_update(const UIState *s);
