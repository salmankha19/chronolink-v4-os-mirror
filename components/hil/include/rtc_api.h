#ifndef HIL_RTC_API_H
#define HIL_RTC_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

bool rtc_init(void);
bool rtc_get_time(rtc_time_t *t);
bool rtc_set_time(const rtc_time_t *t);

/* See chronolink_hal's hal_rtc.h for the full rationale: a DS3231M
   oscillator-stop event means whatever rtc_get_time() returns is not
   "slightly stale", it's meaningless. Call this once after rtc_init()
   and treat the clock as untrustworthy (display "--:--" or similar,
   don't schedule Adhan against it) until a fresh time source has run --
   then call rtc_clear_lost_power_flag() so it doesn't keep tripping.

   Deliberately NOT included here: sync_ntp(). NTP needs the WiFi/
   network stack, which has nothing to do with a hardware RTC chip --
   that belongs in svc_time, not this header. rtc_set_time() is what
   svc_time should call once NTP gives it a real time. */
bool rtc_has_lost_power(bool *lost_power);
bool rtc_clear_lost_power_flag(void);

#ifdef __cplusplus
}
#endif

#endif /* HIL_RTC_API_H */
