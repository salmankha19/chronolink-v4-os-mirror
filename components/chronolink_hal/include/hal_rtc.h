#ifndef CHRONOLINK_HAL_RTC_H
#define CHRONOLINK_HAL_RTC_H

#include <stdint.h>
#include <stdbool.h>
#include "hal.h"

typedef struct {
    uint8_t seconds;   /* 0..59 */
    uint8_t minutes;   /* 0..59 */
    uint8_t hours;     /* 0..23 */
    uint8_t day;       /* day-of-month, 1..31 */
    uint8_t month;     /* 1..12 */
    uint16_t year;     /* full year, 2000..2099 */
} hal_time_t;

/* --------------------------------------------------------------------------
 * Timezone contract
 *
 * hal_time_t carries a wall-clock timestamp with no time-zone information.
 * The HAL does not know, and does not care, whether the caller treats these
 * fields as UTC or local. Convention for this codebase (enforced by svc_time,
 * not by the HAL) is that the DS3231M stores UTC.
 *
 * Time-zone conversion and any daylight-saving adjustment belong in svc_time,
 * on the display path, at read time -- the same model Linux, Android and
 * ESP-IDF's own SNTP example use. Do NOT write a DST-adjusted value into the
 * RTC: the register would then be meaningless without out-of-band metadata,
 * and a device that missed the transition while offline (brownout, no network,
 * powered off across the change) would have no way to recover.
 *
 * Do not add a timezone field or a "this is local time" flag to hal_time_t.
 * That decision belongs to svc_time.
 *
 * No day-of-week field either. The DS3231M has a DOW register (0x03, values
 * 1..7) and HAL_RTC_SetTime() computes and writes a correct value from the
 * date, so the register is never left stale. Nothing reads it today -- a
 * future consumer that needs weekday (e.g. "every Monday" scheduling without
 * date math) should extend this struct and update HAL_RTC_GetTime/SetTime
 * together, not add the field silently. A caller seeing a zero weekday would
 * otherwise have no way to distinguish "not implemented" from "Sunday".
 * -------------------------------------------------------------------------- */

hal_status_t HAL_RTC_Init(void);
hal_status_t HAL_RTC_GetTime(hal_time_t *t);
hal_status_t HAL_RTC_SetTime(const hal_time_t *t);

/* Oscillator-stop-flag (battery-failure) detection.
 *
 * The DS3231M sets OSF (register 0x0F, bit 7) whenever its oscillator has
 * stopped and restarted -- i.e. both mains power AND the backup battery
 * were lost at some point. When that happens, whatever HAL_RTC_GetTime()
 * returns is not "slightly stale", it's meaningless (often 2000-01-01 or
 * frozen at whatever it was when power died). A masjid clock silently
 * displaying a confidently-wrong time is worse than one that visibly
 * shows an error, so the app layer should check this once at boot
 * (after HAL_RTC_Init) and treat the RTC as untrustworthy until a fresh
 * NTP sync has run -- then call HAL_RTC_ClearLostPowerFlag() so the flag
 * doesn't keep tripping on every subsequent boot.
 */
hal_status_t HAL_RTC_HasLostPower(bool *lost_power);
hal_status_t HAL_RTC_ClearLostPowerFlag(void);

#endif // CHRONOLINK_HAL_RTC_H