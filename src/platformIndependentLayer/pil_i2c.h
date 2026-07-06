#ifndef PIL_I2C_H
#define PIL_I2C_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t bus_id;
    uint32_t freq_hz;
} pil_i2c_bus_t;

/* PIL I2C ABI */
bool pil_i2c_init(const pil_i2c_bus_t *bus);
bool pil_i2c_write(uint8_t bus_id, uint8_t addr, const uint8_t *data, uint16_t len);
bool pil_i2c_read(uint8_t bus_id, uint8_t addr, uint8_t *data, uint16_t len);

#endif /* PIL_I2C_H */
