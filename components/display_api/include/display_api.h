#pragma once
#include <stdint.h>
typedef struct { uint16_t w,h; uint8_t format; } DisplayConfig;
int display_init(const DisplayConfig *cfg);
int display_blit_region(const uint8_t *buf, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
int display_submit_frame_async(uint32_t frame_id);
int display_set_brightness(uint8_t level);
void display_register_input_cb(void (*cb)(const void *evt));
