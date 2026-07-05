#include "pil_config.h"
#include "pdl_pins.h"
#include "pdl_partitions.h"
#include <string.h>
#include <stdio.h>

/*
  Minimal stub implementation for pil_config.
  Replace with jsmn-based parser when ready.
  This stub returns defaults and ensures callers can link.
*/

bool pil_config_load(void)
{
    /* TODO: open PDL_PARTITION_CONFIG_PATH, parse JSON, cache values */
    /* For now, always succeed and rely on PDL defaults. */
    return true;
}

int32_t pil_config_get_pin(const char *name, int32_t default_pin)
{
    /* Simple name-based fallback mapping for a few known keys. */
    if (name == NULL) {
        return default_pin;
    }

    if (strcmp(name, "i2c.scl") == 0) return PDL_PIN_I2C_SCL;
    if (strcmp(name, "i2c.sda") == 0) return PDL_PIN_I2C_SDA;
    if (strcmp(name, "spi.sck") == 0) return PDL_PIN_SPI_SCK;
    if (strcmp(name, "spi.mosi") == 0) return PDL_PIN_SPI_MOSI;
    if (strcmp(name, "spi.miso") == 0) return PDL_PIN_SPI_MISO;
    if (strcmp(name, "display.dc") == 0) return PDL_PIN_DISPLAY_DC;
    if (strcmp(name, "display.rst") == 0) return PDL_PIN_DISPLAY_RST;
    if (strcmp(name, "display.cs") == 0) return PDL_PIN_DISPLAY_CS;
    if (strcmp(name, "button.1") == 0) return PDL_PIN_BUTTON_1;
    if (strcmp(name, "button.2") == 0) return PDL_PIN_BUTTON_2;

    return default_pin;
}

int32_t pil_config_get_int(const char *key, int32_t default_value)
{
    (void)key;
    return default_value;
}

bool pil_config_get_bool(const char *key, bool default_value)
{
    (void)key;
    return default_value;
}
