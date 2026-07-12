#ifndef PIL_SENSOR_H
#define PIL_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PIL_SENSOR_TEMP = 0,
    PIL_SENSOR_LIGHT,
    PIL_SENSOR_ACCEL,
} pil_sensor_type_t;

typedef struct {
    pil_sensor_type_t type;
    int32_t value;
} pil_sensor_sample_t;

/* PIL Sensor ABI */
bool pil_sensor_init(void);
bool pil_sensor_read(pil_sensor_type_t type, pil_sensor_sample_t *out);

#endif /* PIL_SENSOR_H */
