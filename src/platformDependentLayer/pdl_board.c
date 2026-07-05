#include "pdl_board.h"

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

void pdl_board_init(void)
{
    /* Board-level init placeholder (clocks, PSRAM, early peripherals). */
}
