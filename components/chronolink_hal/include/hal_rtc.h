#ifndef CHRONOLINK_HAL_RTC_H
#define CHRONOLINK_HAL_RTC_H

#include <stdint.h>
#include "hal.h"

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} hal_time_t;

hal_status_t HAL_RTC_Init(void);
hal_status_t HAL_RTC_GetTime(hal_time_t *t);
hal_status_t HAL_RTC_SetTime(const hal_time_t *t);

#endif
