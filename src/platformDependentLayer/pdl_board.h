#ifndef PDL_BOARD_H
#define PDL_BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Board identity and basic API for board-level initialization */

typedef enum {
    PDL_BOARD_UNKNOWN = 0,
    PDL_BOARD_ESP32S3_DEVKIT,
    PDL_BOARD_CHRONOLINK_V4,
} pdl_board_id_t;

typedef struct {
    pdl_board_id_t id;
    const char *name;
    bool has_rtc;
    bool has_dfplayer;
    bool has_sensors;
} pdl_board_info_t;

const pdl_board_info_t *pdl_board_get_info(void);
esp_err_t pdl_board_init(void);
bool valid_gpio(int pin);

#endif /* PDL_BOARD_H */
