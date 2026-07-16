#ifndef CHRONOLINK_HAL_PDL_PINS_H
#define CHRONOLINK_HAL_PDL_PINS_H

#include "sdkconfig.h"

/* Migrated from src/platformDependentLayer/pdl_pins.h
   Canonical board pin definitions for ChronoLink V4.
   Edit values to match your board wiring if needed. */

#define PDL_PIN_LED_STATUS        2
#define PDL_PIN_SPI_MOSI         23
#define PDL_PIN_SPI_MISO         19
#define PDL_PIN_SPI_SCLK         18
#define PDL_PIN_SPI_CS_DISPLAY   5
#define PDL_PIN_RTC_INT          4
#define PDL_PIN_DISPLAY_DC       16
#define PDL_PIN_DISPLAY_RST      17
#define PDL_PIN_DISPLAY_BL       25

/* Legacy aliases kept for older pil_config callers. */
#define PDL_PIN_SPI_SCK          PDL_PIN_SPI_SCLK
#define PDL_PIN_DISPLAY_CS       PDL_PIN_SPI_CS_DISPLAY
#define PDL_PIN_BUTTON_1         2
#define PDL_PIN_BUTTON_2         3

/* Prefer sdkconfig values when available, otherwise fall back to safe defaults */
#ifndef PDL_PIN_I2C_SCL
#ifdef CONFIG_PDL_PIN_I2C_SCL
#define PDL_PIN_I2C_SCL CONFIG_PDL_PIN_I2C_SCL
#else
#define PDL_PIN_I2C_SCL 22
#endif
#endif

#ifndef PDL_PIN_I2C_SDA
#ifdef CONFIG_PDL_PIN_I2C_SDA
#define PDL_PIN_I2C_SDA CONFIG_PDL_PIN_I2C_SDA
#else
#define PDL_PIN_I2C_SDA 21
#endif
#endif

#ifndef PDL_I2C_FREQ_HZ
#ifdef CONFIG_PDL_I2C_FREQ_HZ
#define PDL_I2C_FREQ_HZ CONFIG_PDL_I2C_FREQ_HZ
#else
#define PDL_I2C_FREQ_HZ 100000
#endif
#endif

/* Safe mode input pin (active low).
   Previously boot_manager used GPIO_NUM_0 directly.
   Keep using GPIO0 here for backward compatibility; change if your board uses a different pin. */
#define PDL_PIN_SAFE_MODE         0

/* Add any additional board-specific pins below */
#endif /* CHRONOLINK_HAL_PDL_PINS_H */
