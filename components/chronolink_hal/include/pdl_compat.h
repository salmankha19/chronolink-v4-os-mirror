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

/* Legacy aliases only. The PIN_* family (PIN_I2C_SDA, PIN_DISPLAY_CS,
   etc.) has moved to components/hil/include/pin_config.h -- the sole
   owner of those names. This file now exists only for the two PDL_PIN_*
   aliases below, retained for older callers. See docs/hal.md. */
#ifndef PDL_PIN_SPI_SCK
#define PDL_PIN_SPI_SCK PDL_PIN_SPI_SCLK
#endif

#ifndef PDL_PIN_DISPLAY_CS
#define PDL_PIN_DISPLAY_CS PDL_PIN_SPI_CS_DISPLAY
#endif

#endif /* CHRONOLINK_HAL_PDL_COMPAT_H */
