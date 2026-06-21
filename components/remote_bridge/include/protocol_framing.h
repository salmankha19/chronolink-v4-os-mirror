#pragma once
#include <stdint.h>
#include <stdbool.h>

#define FRAME_DELIM   0x7E
#define ESCAPE_BYTE   0x7D
#define ESC_XOR       0x20

void framing_init(void);
bool framing_build_and_send(uint8_t type, uint8_t seq, uint16_t trans_id,
                            uint8_t flags, const uint8_t *payload, uint16_t payload_len);
bool framing_try_parse_frame(uint8_t *out_type, uint8_t *out_seq, uint16_t *out_trans_id,
                             uint8_t *payload_buf, uint16_t buf_capacity, uint16_t *out_payload_len);
