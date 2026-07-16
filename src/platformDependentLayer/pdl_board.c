#include "pdl_board.h"
#include "esp_log.h"

/* Minimal board info; extend per-board in future. */

static const pdl_board_info_t g_board_info = {
    .id = PDL_BOARD_CHRONOLINK_V4,
    .name = "ChronoLink V4 (ESP32S3)",
    .has_rtc = true,
    .has_dfplayer = true,
    .has_sensors = true,
};

const pdl_board_info_t *pdl_board_get_info(void)
{
    return &g_board_info;
}

/* Conservative ESP32-S3 GPIO guard */
bool valid_gpio(int pin)
{
    return (pin >= 0 && pin <= 48);
}
