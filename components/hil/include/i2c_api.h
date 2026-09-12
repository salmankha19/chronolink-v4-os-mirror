#ifndef HIL_I2C_API_H
#define HIL_I2C_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t bus_id;
    uint32_t freq_hz;
} i2c_bus_t;

/* Low-level I2C ABI. See i2c_api.c for the real limitation: chronolink_hal
   only exposes ONE physical I2C bus, so bus_id is accepted but ignored
   beyond a warning log if non-zero. */
bool i2c_init(const i2c_bus_t *bus);
bool i2c_write(uint8_t bus_id, uint8_t addr, const uint8_t *data, uint16_t len);
bool i2c_read(uint8_t bus_id, uint8_t addr, uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* HIL_I2C_API_H */
