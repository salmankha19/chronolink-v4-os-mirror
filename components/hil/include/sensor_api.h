#ifndef HIL_SENSOR_API_H
#define HIL_SENSOR_API_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-quantity sensor API. Backed by chronolink_hal's HAL_Sensors_Read(),
   which already aggregates whichever of BME280/SHT4x/VEML7700 are
   compiled in (see components/chronolink_hal/Kconfig -> Sensors). A
   quantity is available if ANY compiled-in sensor reports it -- e.g.
   temperature works whether you have BME280, SHT4x, or both enabled.

   Each getter returns false (and leaves *out unmodified... actually
   sets *out = 0.0f on failure) if no compiled-in sensor provides that
   quantity, or if the read itself failed. Check the return value --
   don't assume 0.0f means "it's freezing", it means "no reading". */

bool sensor_get_temperature(float *celsius);
bool sensor_get_humidity(float *percent_rh);

/* Pressure: only BME280 provides this. Returns false on any board
   without a BME280 compiled in (CONFIG_CHRONOLINK_HAL_SENSOR_BME280),
   including boards using only SHT4x. */
bool sensor_get_pressure(float *hpa);

/* Light: only VEML7700 provides this. Returns false if
   CONFIG_CHRONOLINK_HAL_SENSOR_VEML7700 is not enabled. */
bool sensor_get_light(float *lux);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SENSOR_API_H */
