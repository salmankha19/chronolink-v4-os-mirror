#ifndef HAL_REGISTRY_H
#define HAL_REGISTRY_H

#include "hal_driver_ext.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_registry hal_registry_t;

/* Create/destroy a registry instance.
   max_entries: maximum number of role registrations in this registry.
   Returns NULL on allocation failure. */
hal_registry_t *hal_registry_create(size_t max_entries);

/* Destroy registry and deinit any active drivers. */
void hal_registry_destroy(hal_registry_t *reg);

/* Initialize registry (optional if create already initializes). */
int hal_registry_init(hal_registry_t *reg);

/* Shutdown registry (deinit drivers but keep instance) */
int hal_registry_shutdown(hal_registry_t *reg);

/* Register a driver for a logical role.
   Returns HAL_OK on success, HAL_ERR on failure. */
hal_status_t hal_registry_register_driver(hal_registry_t *reg,
                                         const char *role,
                                         const hal_driver_t *drv,
                                         const hal_config_t *cfg);

/* Unregister a role (deinit driver if active). */
hal_status_t hal_registry_unregister_role(hal_registry_t *reg, const char *role);

/* Lookup driver pointer for a role (thread-safe). */
const hal_driver_t *hal_registry_get_driver(const hal_registry_t *reg, const char *role);

/* Read role (for sensors). Caller provides sensor_reading_t* out. */
hal_status_t hal_registry_read_role(hal_registry_t *reg, const char *role, sensor_reading_t *out);

/* Write role (for displays / generic writes). */
hal_status_t hal_registry_write_role(hal_registry_t *reg, const char *role, const void *data, size_t len);

/* Query capabilities for a role. */
hal_status_t hal_registry_get_caps(hal_registry_t *reg, const char *role, device_caps_t *out);

/* Iterate roles: callback(role, drv, cfg, user_ctx). Return non-zero to stop iteration early. */
typedef int (*hal_registry_iter_cb)(const char *role, const hal_driver_t *drv, const hal_config_t *cfg, void *user_ctx);
void hal_registry_iterate(hal_registry_t *reg, hal_registry_iter_cb cb, void *user_ctx);

#ifdef __cplusplus
}
#endif

#endif /* HAL_REGISTRY_H */
