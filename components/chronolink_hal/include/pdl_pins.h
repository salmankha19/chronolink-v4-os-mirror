#ifndef CHRONOLINK_HAL_PDL_PINS_H
#define CHRONOLINK_HAL_PDL_PINS_H

#include "sdkconfig.h"

/* ChronoLink V4 PCB pin map — verified from schematic.
   Chip: ESP32-S3-WROOM-1 N16R8.
   Avoid GPIO 35..37 (PSRAM-connected).
   Avoid GPIO 43/44 (default console UART). */

/* --- Status LED: not present on v4 PCB (IO2 = LCD backlight) --- */
#define PDL_PIN_LED_STATUS       (-1)

/* --- SPI bus (shared: ST7796S TFT + SD card) --- */
#define PDL_PIN_SPI_MOSI          5
#define PDL_PIN_SPI_MISO          6
#define PDL_PIN_SPI_SCLK          4

/* --- TFT ST7796S control --- */
#define PDL_PIN_SPI_CS_DISPLAY    7
#define PDL_PIN_DISPLAY_DC       10
#define PDL_PIN_DISPLAY_RST      16
#define PDL_PIN_DISPLAY_BL        8

/* --- SD card --- */
#define PDL_PIN_SD_CS             9

/* --- I2C (sensors + RTC) --- */
#ifndef PDL_PIN_I2C_SCL
#ifdef CONFIG_PDL_PIN_I2C_SCL
#define PDL_PIN_I2C_SCL CONFIG_PDL_PIN_I2C_SCL
#else
#define PDL_PIN_I2C_SCL          17
#endif
#endif

#ifndef PDL_PIN_I2C_SDA
#ifdef CONFIG_PDL_PIN_I2C_SDA
#define PDL_PIN_I2C_SDA CONFIG_PDL_PIN_I2C_SDA
#else
#define PDL_PIN_I2C_SDA          18
#endif
#endif

#ifndef PDL_I2C_FREQ_HZ
#ifdef CONFIG_PDL_I2C_FREQ_HZ
#define PDL_I2C_FREQ_HZ CONFIG_PDL_I2C_FREQ_HZ
#else
#define PDL_I2C_FREQ_HZ      100000
#endif
#endif

/* --- Audio (MAX98357A I2S) ---
 * Schematic marks SD_MODE as "Not using SD_Mode pin". */
#define PDL_PIN_I2S_BCLK         47
#define PDL_PIN_I2S_WS           48
#define PDL_PIN_I2S_DOUT         21
#define PDL_PIN_AUDIO_SD_MODE   (-1)

/* --- RTC INT: not wired on v4 PCB --- */
#define PDL_PIN_RTC_INT         (-1)

/* --- Safe mode (active low) — BOOT button on IO0 --- */
#define PDL_PIN_SAFE_MODE         0

/* --- Legacy aliases --- */
#define PDL_PIN_SPI_SCK          PDL_PIN_SPI_SCLK
#define PDL_PIN_DISPLAY_CS       PDL_PIN_SPI_CS_DISPLAY

#endif /* CHRONOLINK_HAL_PDL_PINS_H */