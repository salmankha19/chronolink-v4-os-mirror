/*
 * rtc_api.c
 *
 * Thin adapter over chronolink_hal's HAL_RTC_* (hal_rtc.h /
 * hal_rtc_ds3231m.c -- the production-quality DS3231M driver).
 */
#include "rtc_api.h"
#include "hal_rtc.h"

bool rtc_init(void)
{
    return HAL_RTC_Init() == HAL_OK;
}

bool rtc_get_time(rtc_time_t *t)
{
    if (!t) return false;
    hal_time_t ht;
    if (HAL_RTC_GetTime(&ht) != HAL_OK) return false;

    t->seconds = ht.seconds;
    t->minutes = ht.minutes;
    t->hours   = ht.hours;
    t->day     = ht.day;
    t->month   = ht.month;
    t->year    = ht.year;
    return true;
}

bool rtc_set_time(const rtc_time_t *t)
{
    if (!t) return false;
    hal_time_t ht = {
        .seconds = t->seconds,
        .minutes = t->minutes,
        .hours   = t->hours,
        .day     = t->day,
        .month   = t->month,
        .year    = t->year,
    };
    return HAL_RTC_SetTime(&ht) == HAL_OK;
}

bool rtc_has_lost_power(bool *lost_power)
{
    if (!lost_power) return false;
    return HAL_RTC_HasLostPower(lost_power) == HAL_OK;
}

bool rtc_clear_lost_power_flag(void)
{
    return HAL_RTC_ClearLostPowerFlag() == HAL_OK;
}
