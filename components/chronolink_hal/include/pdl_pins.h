#ifndef CHRONOLINK_HAL_PDL_PINS_H
#define CHRONOLINK_HAL_PDL_PINS_H

/* Migrated from src/platformDependentLayer/pdl_pins.h
   Canonical board pin definitions for ChronoLink V4.
   Edit values to match your board wiring if needed. */

#define PDL_PIN_LED_STATUS        2
#define PDL_PIN_I2C_SDA          21
#define PDL_PIN_I2C_SCL          22
#define PDL_PIN_SPI_MOSI         23
#define PDL_PIN_SPI_MISO         19
#define PDL_PIN_SPI_SCLK         18
#define PDL_PIN_SPI_CS_DISPLAY   5
#define PDL_PIN_RTC_INT          4
#define PDL_PIN_DISPLAY_DC       16
#define PDL_PIN_DISPLAY_RST      17
#define PDL_PIN_DISPLAY_BL       25

/* Safe mode input pin (active low).
   Previously boot_manager used GPIO_NUM_0 directly.
   Keep using GPIO0 here for backward compatibility; change if your board uses a different pin. */
#define PDL_PIN_SAFE_MODE         0

/* Add any additional board-specific pins below */
#endif /* CHRONOLINK_HAL_PDL_PINS_H */
