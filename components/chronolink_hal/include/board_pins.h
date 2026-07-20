#pragma once

#include "sdkconfig.h"
#include "pdl_pins.h"
#include <stdint.h>

/* LCD control config fallbacks (preserve existing behavior) */
#ifndef CONFIG_BOARD_LCD_CS
#define CONFIG_BOARD_LCD_CS  PDL_PIN_SPI_CS_DISPLAY
#endif

#ifndef CONFIG_BOARD_LCD_DC
#define CONFIG_BOARD_LCD_DC  PDL_PIN_DISPLAY_DC
#endif

#ifndef CONFIG_BOARD_LCD_RST
#define CONFIG_BOARD_LCD_RST  PDL_PIN_DISPLAY_RST
#endif

#ifndef CONFIG_BOARD_LCD_BL
#define CONFIG_BOARD_LCD_BL  PDL_PIN_DISPLAY_BL
#endif

/* Resolve BOARD_LCD_* at compile time */
#ifndef BOARD_LCD_CS
#define BOARD_LCD_CS   CONFIG_BOARD_LCD_CS
#endif

#ifndef BOARD_LCD_DC
#define BOARD_LCD_DC   CONFIG_BOARD_LCD_DC
#endif

#ifndef BOARD_LCD_RST
#define BOARD_LCD_RST  CONFIG_BOARD_LCD_RST
#endif

#ifndef BOARD_LCD_BL
#define BOARD_LCD_BL   CONFIG_BOARD_LCD_BL
#endif

/* --------------------------------------------------------------------------
  I2C pin mapping and frequency
  - Prefer CONFIG_PDL_PIN_* if present (new Kconfig)
  - Otherwise accept CONFIG_BOARD_I2C_* if present
  - Otherwise fall back to safe dev defaults
  - Use #ifndef BOARD_I2C_* to avoid redefinition errors
  -------------------------------------------------------------------------- */

/* BOARD_I2C_SDA */
#ifndef BOARD_I2C_SDA
  #if defined(CONFIG_PDL_PIN_I2C_SDA)
    #define BOARD_I2C_SDA CONFIG_PDL_PIN_I2C_SDA
  #elif defined(CONFIG_BOARD_I2C_SDA)
    #define BOARD_I2C_SDA CONFIG_BOARD_I2C_SDA
  #else
    /* safe dev default */
    #define BOARD_I2C_SDA 21
  #endif
#endif

/* BOARD_I2C_SCL */
#ifndef BOARD_I2C_SCL
  #if defined(CONFIG_PDL_PIN_I2C_SCL)
    #define BOARD_I2C_SCL CONFIG_PDL_PIN_I2C_SCL
  #elif defined(CONFIG_BOARD_I2C_SCL)
    #define BOARD_I2C_SCL CONFIG_BOARD_I2C_SCL
  #else
    /* safe dev default */
    #define BOARD_I2C_SCL 26
  #endif
#endif

/* BOARD_I2C_FREQ_HZ */
#ifndef BOARD_I2C_FREQ_HZ
  #if defined(CONFIG_PDL_I2C_FREQ_HZ)
    #define BOARD_I2C_FREQ_HZ CONFIG_PDL_I2C_FREQ_HZ
  #elif defined(CONFIG_BOARD_I2C_FREQ_HZ)
    #define BOARD_I2C_FREQ_HZ CONFIG_BOARD_I2C_FREQ_HZ
  #else
    #define BOARD_I2C_FREQ_HZ 100000
  #endif
#endif