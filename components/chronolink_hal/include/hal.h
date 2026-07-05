#ifndef CHRONOLINK_HAL_H
#define CHRONOLINK_HAL_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HAL_OK = 0,
    HAL_ERR_INIT = -1,
    HAL_ERR_BUS  = -2,
    HAL_ERR_DEV  = -3
} hal_status_t;

// Global init
hal_status_t HAL_Init(void);

#endif
