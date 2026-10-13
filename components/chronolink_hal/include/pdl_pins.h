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
 * SD_MODE (MAX98357A pin 4) is held HIGH on the shipped PCB by R21
 * (0R) to +3V3, so the amplifier is always enabled (right-channel
 * output). The amplifier is not under firmware control.
 *
 * ESP32-S3 GPIO1 is routed to JP2 (a 3-pad solder jumper, "Bridged12"
 * by default). JP2 pad 1 = R21 to +3V3, pad 2 = amplifier *SD_MODE,
 * pad 3 = GPIO1's trace. R15 (10kR) sits on the SD_MODE net (pad 2),
 * not on GPIO1's trace. With pads 1-2 bridged (default), GPIO1 is
 * isolated from the amplifier and from R15 -- GPIO1 floats.
 *
 * To enable firmware control of SD_MODE:
 *   1. Cut the JP2 1-2 bridge (open the solder). This disconnects
 *      R21 (0R) from SD_MODE. R21 stays on pad 1, which is now
 *      isolated -- no desoldering of R21 is required.
 *   2. Solder the JP2 2-3 bridge, connecting amplifier *SD_MODE
 *      (pad 2) to GPIO1's trace (pad 3).
 *   3. Keep R15 (10kR) in place: it is on the SD_MODE net and pulls
 *      SD_MODE low when GPIO1 is not driven, which is the desired
 *      shutdown state.
 *   4. In firmware: change PDL_PIN_AUDIO_SD_MODE from -1 to 1, and
 *      re-enable the output config in HAL_Audio_Init().
 *      A HAL/HIL setter for enable/shutdown would also be needed --
 *      this codebase does not have one today.
 *
 * The default (-1) reflects the shipped hardware and keeps the
 * amplifier permanently enabled. See docs/hal.md for the full note. */
#define PDL_PIN_I2S_BCLK         47
#define PDL_PIN_I2S_WS           48
#define PDL_PIN_I2S_DOUT         21
#define PDL_PIN_AUDIO_SD_MODE   (-1)

/* --- RTC INT: not wired on v4 PCB --- */
#define PDL_PIN_RTC_INT         (-1)

/* --- BOOT button on IO0 (active low). Development/recovery only:
       the ROM bootloader samples GPIO0 at reset to enter serial-download
       mode. No firmware path reads it; kept so PDL pin validation and the
       pin debug dump still cover GPIO0. --- */
#define PDL_PIN_SAFE_MODE         0

/* --- Legacy aliases --- */
#define PDL_PIN_SPI_SCK          PDL_PIN_SPI_SCLK
#define PDL_PIN_DISPLAY_CS       PDL_PIN_SPI_CS_DISPLAY

#endif /* CHRONOLINK_HAL_PDL_PINS_H */