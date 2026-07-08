#include "hal_rtc.h"

hal_status_t HAL_RTC_Init(void)
{
    return HAL_OK;
}

hal_status_t HAL_RTC_GetTime(hal_time_t *t)
{
    // Stub
    t->hours = 12;
    t->minutes = 0;
    t->seconds = 0;
    return HAL_OK;
}
