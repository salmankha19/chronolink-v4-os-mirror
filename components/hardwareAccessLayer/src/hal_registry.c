/* hal_registry.c - multi-instance HAL registry reference implementation */
#include "hal_registry.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef HAL_REGISTRY_NO_FREERTOS
/* Default: use FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define HAL_REG_LOCK_T SemaphoreHandle_t
#define HAL_REG_LOCK_CREATE() xSemaphoreCreateMutex()
#define HAL_REG_LOCK_TAKE(h) xSemaphoreTake((h), portMAX_DELAY)
#define HAL_REG_LOCK_GIVE(h) xSemaphoreGive((h))
#define HAL_REG_LOCK_DELETE(h) vSemaphoreDelete((h))
#else
/* Fallback: pthreads (replace with platform lock if needed) */
#include <pthread.h>
#define HAL_REG_LOCK_T pthread_mutex_t
static HAL_REG_LOCK_T hal_reg_lock_create_impl(void) {
    HAL_REG_LOCK_T m = PTHREAD_MUTEX_INITIALIZER;
    return m;
}
#define HAL_REG_LOCK_CREATE() ({ HAL_REG_LOCK_T *p = malloc(sizeof(HAL_REG_LOCK_T)); pthread_mutex_init(p, NULL); p; })
#define HAL_REG_LOCK_TAKE(h) pthread_mutex_lock((h))
#define HAL_REG_LOCK_GIVE(h) pthread_mutex_unlock((h))
#define HAL_REG_LOCK_DELETE(h) do { pthread_mutex_destroy((h)); free((h)); } while(0)
#endif

/* Internal entry */
typedef struct {
    char *role;
    const hal_driver_t *drv;
    hal_config_t cfg;
    bool active;
} registry_entry_t;

struct hal_registry {
    registry_entry_t *entries;
    size_t max_entries;
    HAL_REG_LOCK_T lock;
    bool inited;
};

/* Create instance */
hal_registry_t *hal_registry_create(size_t max_entries) {
    if (max_entries == 0) return NULL;
    hal_registry_t *r = (hal_registry_t*)calloc(1, sizeof(hal_registry_t));
    if (!r) return NULL;
    r->entries = (registry_entry_t*)calloc(max_entries, sizeof(registry_entry_t));
    if (!r->entries) { free(r); return NULL; }
    r->max_entries = max_entries;
    r->inited = false;
    /* create lock */
    #ifndef HAL_REGISTRY_NO_FREERTOS
    r->lock = HAL_REG_LOCK_CREATE();
    if (!r->lock) { free(r->entries); free(r); return NULL; }
    #else
    r->lock = HAL_REG_LOCK_CREATE();
    if (!r->lock) { free(r->entries); free(r); return NULL; }
    #endif
    r->inited = true;
    return r;
}

/* Destroy instance */
void hal_registry_destroy(hal_registry_t *reg) {
    if (!reg) return;
    HAL_REG_LOCK_TAKE(reg->lock);
    for (size_t i = 0; i < reg->max_entries; ++i) {
        if (reg->entries[i].active) {
            if (reg->entries[i].drv && reg->entries[i].drv->deinit) {
                reg->entries[i].drv->deinit();
            }
            free(reg->entries[i].role);
            reg->entries[i].active = false;
        }
    }
    HAL_REG_LOCK_GIVE(reg->lock);
    HAL_REG_LOCK_DELETE(reg->lock);
    free(reg->entries);
    free(reg);
}

/* Helper: find index by role */
static int find_index_by_role(const hal_registry_t *reg, const char *role) {
    if (!reg || !role) return -1;
    for (size_t i = 0; i < reg->max_entries; ++i) {
        if (reg->entries[i].active && reg->entries[i].role && strcmp(reg->entries[i].role, role) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Helper: find free slot */
static int find_free_slot(const hal_registry_t *reg) {
    for (size_t i = 0; i < reg->max_entries; ++i) {
        if (!reg->entries[i].active) return (int)i;
    }
    return -1;
}

int hal_registry_init(hal_registry_t *reg) {
    if (!reg) return -1;
    if (reg->inited) return 0;
    /* nothing else to do for now */
    reg->inited = true;
    return 0;
}

int hal_registry_shutdown(hal_registry_t *reg) {
    if (!reg) return -1;
    HAL_REG_LOCK_TAKE(reg->lock);
    for (size_t i = 0; i < reg->max_entries; ++i) {
        if (reg->entries[i].active && reg->entries[i].drv && reg->entries[i].drv->deinit) {
            reg->entries[i].drv->deinit();
            reg->entries[i].active = false;
            free(reg->entries[i].role);
            reg->entries[i].role = NULL;
        }
    }
    HAL_REG_LOCK_GIVE(reg->lock);
    reg->inited = false;
    return 0;
}

hal_status_t hal_registry_register_driver(hal_registry_t *reg,
                                         const char *role,
                                         const hal_driver_t *drv,
                                         const hal_config_t *cfg) {
    if (!reg || !role || !drv) return HAL_ERR;
    HAL_REG_LOCK_TAKE(reg->lock);

    /* duplicate check */
    if (find_index_by_role(reg, role) >= 0) {
        HAL_REG_LOCK_GIVE(reg->lock);
        return HAL_ERR;
    }

    int slot = find_free_slot(reg);
    if (slot < 0) { HAL_REG_LOCK_GIVE(reg->lock); return HAL_ERR; }

    /* allocate role string */
    reg->entries[slot].role = strdup(role);
    reg->entries[slot].drv = drv;
    if (cfg) reg->entries[slot].cfg = *cfg;
    reg->entries[slot].active = false;

    /* init driver */
    if (drv->init) {
        if (drv->init(&reg->entries[slot].cfg) != HAL_OK) {
            free(reg->entries[slot].role);
            reg->entries[slot].role = NULL;
            reg->entries[slot].drv = NULL;
            reg->entries[slot].active = false;
            HAL_REG_LOCK_GIVE(reg->lock);
            return HAL_ERR;
        }
    }
    reg->entries[slot].active = true;
    HAL_REG_LOCK_GIVE(reg->lock);
    return HAL_OK;
}

hal_status_t hal_registry_unregister_role(hal_registry_t *reg, const char *role) {
    if (!reg || !role) return HAL_ERR;
    HAL_REG_LOCK_TAKE(reg->lock);
    int idx = find_index_by_role(reg, role);
    if (idx < 0) { HAL_REG_LOCK_GIVE(reg->lock); return HAL_NOT_FOUND; }
    if (reg->entries[idx].drv && reg->entries[idx].drv->deinit) reg->entries[idx].drv->deinit();
    free(reg->entries[idx].role);
    reg->entries[idx].role = NULL;
    reg->entries[idx].drv = NULL;
    reg->entries[idx].active = false;
    HAL_REG_LOCK_GIVE(reg->lock);
    return HAL_OK;
}

const hal_driver_t *hal_registry_get_driver(const hal_registry_t *reg, const char *role) {
    if (!reg || !role) return NULL;
    const hal_driver_t *drv = NULL;
    HAL_REG_LOCK_TAKE(((hal_registry_t*)reg)->lock);
    int idx = find_index_by_role(reg, role);
    if (idx >= 0) drv = reg->entries[idx].drv;
    HAL_REG_LOCK_GIVE(((hal_registry_t*)reg)->lock);
    return drv;
}

hal_status_t hal_registry_read_role(hal_registry_t *reg, const char *role, sensor_reading_t *out) {
    if (!reg || !role || !out) return HAL_ERR;
    HAL_REG_LOCK_TAKE(reg->lock);
    int idx = find_index_by_role(reg, role);
    if (idx < 0) { HAL_REG_LOCK_GIVE(reg->lock); return HAL_NOT_FOUND; }
    const hal_driver_t *drv = reg->entries[idx].drv;
    HAL_REG_LOCK_GIVE(reg->lock);
    if (!drv || !drv->read) return HAL_UNSUPPORTED;
    return drv->read((void*)out);
}

hal_status_t hal_registry_write_role(hal_registry_t *reg, const char *role, const void *data, size_t len) {
    if (!reg || !role) return HAL_ERR;
    HAL_REG_LOCK_TAKE(reg->lock);
    int idx = find_index_by_role(reg, role);
    if (idx < 0) { HAL_REG_LOCK_GIVE(reg->lock); return HAL_NOT_FOUND; }
    const hal_driver_t *drv = reg->entries[idx].drv;
    HAL_REG_LOCK_GIVE(reg->lock);
    if (!drv || !drv->write) return HAL_UNSUPPORTED;
    return drv->write(data, len);
}

hal_status_t hal_registry_get_caps(hal_registry_t *reg, const char *role, device_caps_t *out) {
    if (!reg || !role || !out) return HAL_ERR;
    HAL_REG_LOCK_TAKE(reg->lock);
    int idx = find_index_by_role(reg, role);
    if (idx < 0) { HAL_REG_LOCK_GIVE(reg->lock); return HAL_NOT_FOUND; }
    const hal_driver_t *drv = reg->entries[idx].drv;
    HAL_REG_LOCK_GIVE(reg->lock);
    if (!drv || !drv->get_capabilities) return HAL_UNSUPPORTED;
    return drv->get_capabilities(out);
}

void hal_registry_iterate(hal_registry_t *reg, hal_registry_iter_cb cb, void *user_ctx) {
    if (!reg || !cb) return;
    HAL_REG_LOCK_TAKE(reg->lock);
    for (size_t i = 0; i < reg->max_entries; ++i) {
        if (reg->entries[i].active) {
            int stop = cb(reg->entries[i].role, reg->entries[i].drv, &reg->entries[i].cfg, user_ctx);
            if (stop) break;
        }
    }
    HAL_REG_LOCK_GIVE(reg->lock);
}