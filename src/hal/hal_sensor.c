/* HAL Sensor driver placeholder. Implement sensor drivers here. */

#include "pil_sensor.h"

bool pil_sensor_init(void) { return true; }
bool pil_sensor_read(pil_sensor_type_t type, pil_sensor_sample_t *out)
{ (void)type; if (out) out->value = 0; return false; }
