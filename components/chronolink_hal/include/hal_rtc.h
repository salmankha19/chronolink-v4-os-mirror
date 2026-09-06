#ifndef CHRONOLINK_HAL_RTC_H
#define CHRONOLINK_HAL_RTC_H

#include <stdint.h>
#include <stdbool.h>
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

/* Oscillator-stop-flag (battery-failure) detection.
 *
 * The DS3231M sets OSF (register 0x0F, bit 7) whenever its oscillator has
 * stopped and restarted — i.e. both mains power AND the backup battery
 * were lost at some point. When that happens, whatever HAL_RTC_GetTime()
 * returns is not "slightly stale", it's meaningless (often 2000-01-01 or
 * frozen at whatever it was when power died). A masjid clock silently
 * displaying a confidently-wrong time is worse than one that visibly
 * shows an error, so the app layer should check this once at boot
 * (after HAL_RTC_Init) and treat the RTC as untrustworthy until a fresh
 * NTP sync has run — then call HAL_RTC_ClearLostPowerFlag() so the flag
 * doesn't keep tripping on every subsequent boot.
 */
hal_status_t HAL_RTC_HasLostPower(bool *lost_power);
hal_status_t HAL_RTC_ClearLostPowerFlag(void);

#endif
