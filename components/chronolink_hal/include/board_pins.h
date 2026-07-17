#pragma once

#include "sdkconfig.h"
#include "pdl_pins.h"

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

#ifndef CONFIG_BOARD_I2C_SDA
#define CONFIG_BOARD_I2C_SDA  PDL_PIN_I2C_SDA
#endif

#ifndef CONFIG_BOARD_I2C_SCL
#define CONFIG_BOARD_I2C_SCL  PDL_PIN_I2C_SCL
#endif

/* Board pins resolved at compile time from Kconfig. */
#define BOARD_LCD_CS   CONFIG_BOARD_LCD_CS
#define BOARD_LCD_DC   CONFIG_BOARD_LCD_DC
#define BOARD_LCD_RST  CONFIG_BOARD_LCD_RST
#define BOARD_LCD_BL   CONFIG_BOARD_LCD_BL

#define BOARD_I2C_SDA  CONFIG_BOARD_I2C_SDA
#define BOARD_I2C_SCL  CONFIG_BOARD_I2C_SCL

/* Kconfig-driven pin mapping with safe fallbacks */

/* If Kconfig defines PDL_PIN_I2C_SDA/SCL, use those; otherwise fall back to safe defaults */
#ifdef CONFIG_PDL_PIN_I2C_SDA
#define BOARD_I2C_SDA CONFIG_PDL_PIN_I2C_SDA
#else
#define BOARD_I2C_SDA 21
#endif

#ifdef CONFIG_PDL_PIN_I2C_SCL
#define BOARD_I2C_SCL CONFIG_PDL_PIN_I2C_SCL
#else
#define BOARD_I2C_SCL 22
#endif

/* Frequency fallback */
#ifdef CONFIG_PDL_I2C_FREQ_HZ
#define BOARD_I2C_FREQ_HZ CONFIG_PDL_I2C_FREQ_HZ
#else
#define BOARD_I2C_FREQ_HZ 100000
#endif
