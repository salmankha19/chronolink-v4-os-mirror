#ifndef PIL_CONFIG_H
#define PIL_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* Load runtime configuration (pins.json). Returns true on success or fallback. */
bool pil_config_load(void);

/* Lookup helpers: return default when key not present. */
int32_t pil_config_get_pin(const char *name, int32_t default_pin);
int32_t pil_config_get_int(const char *key, int32_t default_value);
bool    pil_config_get_bool(const char *key, bool default_value);

#endif /* PIL_CONFIG_H */
