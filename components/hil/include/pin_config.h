/*
 * pin_config.h
 *
 * ChronoLink V4 OS — HIL pin name aliases
 *
 * IMPORTANT: this file does NOT define its own Kconfig entries. The
 * single source of truth for every pin is the "ChronoLink HAL Board
 * Pins" menu in the top-level Kconfig.projbuild, consumed by
 * chronolink_hal's board_pins.h / pdl_pins.h. This header only gives
 * those same values more readable names for HIL/service/app code to
 * use, so there is exactly one place to ever change a pin -- an
 * earlier version of this codebase had two separate Kconfig prompts
 * for the same I2C pins that silently fought each other; this file
 * exists specifically to avoid recreating that bug for display/audio
 * pins too.
 *
 * If you need a pin that isn't aliased here yet, add the alias -- do
 * NOT add a new `config` block anywhere else for a pin that already
 * has one in Kconfig.projbuild.
 */
#pragma once

#include "sdkconfig.h"
#include "board_pins.h"   /* chronolink_hal: BOARD_LCD_*, BOARD_I2C_* */

#ifdef __cplusplus
extern "C" {
#endif

/* Display (ST7796S) -- source: BOARD_LCD_* */
#define PIN_DISPLAY_CS   BOARD_LCD_CS
#define PIN_DISPLAY_DC   BOARD_LCD_DC
#define PIN_DISPLAY_RST  BOARD_LCD_RST
#define PIN_DISPLAY_BL   BOARD_LCD_BL

/* I2C bus (sensors) -- source: BOARD_I2C_* */
#define PIN_I2C_SDA      BOARD_I2C_SDA
#define PIN_I2C_SCL      BOARD_I2C_SCL
#ifdef CONFIG_PDL_I2C_FREQ_HZ
#define PIN_I2C_FREQ_HZ  CONFIG_PDL_I2C_FREQ_HZ
#else
#define PIN_I2C_FREQ_HZ  100000
#endif

/* Audio (MAX98357A) -- source: BOARD_AUDIO_* (see Kconfig.projbuild;
   previously these had NO Kconfig entry at all -- fixed alongside
   this file, not just aliased). */
#ifdef CONFIG_BOARD_AUDIO_I2S_BCLK
#define PIN_AUDIO_BCLK   CONFIG_BOARD_AUDIO_I2S_BCLK
#else
#define PIN_AUDIO_BCLK   26
#endif

#ifdef CONFIG_BOARD_AUDIO_I2S_WS
#define PIN_AUDIO_LRCK   CONFIG_BOARD_AUDIO_I2S_WS
#else
#define PIN_AUDIO_LRCK   25
#endif

#ifdef CONFIG_BOARD_AUDIO_I2S_DOUT
#define PIN_AUDIO_DOUT   CONFIG_BOARD_AUDIO_I2S_DOUT
#else
#define PIN_AUDIO_DOUT   27
#endif

#ifdef CONFIG_BOARD_AUDIO_SD_MODE
#define PIN_AUDIO_SD_MODE CONFIG_BOARD_AUDIO_SD_MODE
#else
#define PIN_AUDIO_SD_MODE (-1)
#endif

#ifdef __cplusplus
}
#endif
