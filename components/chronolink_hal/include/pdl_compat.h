#ifndef CHRONOLINK_HAL_PDL_COMPAT_H
#define CHRONOLINK_HAL_PDL_COMPAT_H

#include "esp_err.h"

/* Compatibility shim for legacy includes.
   Map old legacy header names to the new component headers. */

#ifndef PDL_PINS_H
#define PDL_PINS_H
#include "pdl_pins.h"
#endif

#ifndef PDL_CAPABILITIES_H
#define PDL_CAPABILITIES_H
#include "pdl_capabilities.h"
#endif

#ifndef PDL_PARTITIONS_H
#define PDL_PARTITIONS_H
#include "pdl_partitions.h"
#endif

esp_err_t pdl_board_init(void);

/* Legacy symbol aliases used by older platform-dependent callers. */
#ifndef PDL_PIN_SPI_SCK
#define PDL_PIN_SPI_SCK PDL_PIN_SPI_SCLK
#endif

#ifndef PDL_PIN_DISPLAY_CS
#define PDL_PIN_DISPLAY_CS PDL_PIN_SPI_CS_DISPLAY
#endif

#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA PDL_PIN_I2C_SDA
#endif

#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL PDL_PIN_I2C_SCL
#endif

#ifndef PIN_SPI_MOSI
#define PIN_SPI_MOSI PDL_PIN_SPI_MOSI
#endif

#ifndef PIN_SPI_MISO
#define PIN_SPI_MISO PDL_PIN_SPI_MISO
#endif

#ifndef PIN_SPI_SCLK
#define PIN_SPI_SCLK PDL_PIN_SPI_SCLK
#endif

#ifndef PIN_SPI_CS
#define PIN_SPI_CS PDL_PIN_SPI_CS_DISPLAY
#endif

#ifndef PIN_DISPLAY_DC
#define PIN_DISPLAY_DC PDL_PIN_DISPLAY_DC
#endif

#ifndef PIN_DISPLAY_RST
#define PIN_DISPLAY_RST PDL_PIN_DISPLAY_RST
#endif

#ifndef PIN_DISPLAY_BL
#define PIN_DISPLAY_BL PDL_PIN_DISPLAY_BL
#endif

#ifndef PIN_RTC_INT
#define PIN_RTC_INT PDL_PIN_RTC_INT
#endif

#endif /* CHRONOLINK_HAL_PDL_COMPAT_H */
