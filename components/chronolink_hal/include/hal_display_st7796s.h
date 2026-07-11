#pragma once

#include <stdint.h>
#include <stddef.h>

void st7796s_init(void);
void st7796s_reset(void);
void st7796s_send_command(uint8_t cmd);
void st7796s_send_data(const uint8_t *data, size_t len);