/*
 * hal_rtc_ds3231m.c
 *
 * ChronoLink V4 OS -- DS3231M RTC Driver (I2C)
 *
 * Address: 0x68
 * Time registers: 0x00-0x06 (BCD)
 *
 * Contract: this driver stores and returns UTC. No time-zone or DST
 * logic lives here -- see hal_rtc.h for the rationale and svc_time for
 * where that logic belongs.
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#include "hal_rtc.h"
#include "hal_i2c.h"
#include "esp_log.h"

static const char *TAG = "HAL_RTC";
#define DS3231M_ADDR 0x68
#define DS3231M_REG_STATUS 0x0F
#define DS3231M_OSF_BIT (1U << 7)

/* Set by HAL_RTC_Init() after the probe. If the chip doesn't answer,
   every subsequent operation short-circuits instead of re-issuing I2C
   and re-failing -- in a build where the RTC is unpopulated, svc_time
   polling once per second would otherwise produce a wall of errors. */
static bool s_rtc_present = false;

static inline uint8_t bcd2bin(uint8_t bcd)
{
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

static inline uint8_t bin2bcd(uint8_t bin)
{
    return ((bin / 10) << 4) | (bin % 10);
}

/* DS3231M day-of-week register (0x03) expects 1..7. Convention here is
   ISO 8601: Monday = 1 .. Sunday = 7. Computed from the date being
   written so the DOW register can never disagree with the calendar date.
 *
 * Zeller's congruence, adjusted for ISO Monday=1.
 *   h = (q + floor(13(m+1)/5) + K + floor(K/4) + floor(J/4) + 5J) mod 7
 * gives h with 0=Saturday..6=Friday. Rotate to ISO: ((h + 5) % 7) + 1.
 *
 * Callers validate year/month/day ranges before reaching this function,
 * so no defensive checks here. */
static uint8_t dow_from_ymd(uint16_t year, uint8_t month, uint8_t day)
{
    int y = (int)year;
    int m = (int)month;
    if (m < 3) { m += 12; y -= 1; }
    int K = y % 100;
    int J = y / 100;
    int h = ((int)day + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    /* h: 0=Sat, 1=Sun, 2=Mon, 3=Tue, 4=Wed, 5=Thu, 6=Fri
       ISO: Mon=1, Tue=2, ..., Sun=7
       verified: h=2 (Mon) -> 1,  h=0 (Sat) -> 6,  h=1 (Sun) -> 7 */
    return (uint8_t)(((h + 5) % 7) + 1);
}

hal_status_t HAL_RTC_HasLostPower(bool *lost_power)
{
    if (!lost_power) return HAL_ERR_DEV;
    if (!s_rtc_present) return HAL_ERR_INIT;

    uint8_t status_reg;
    hal_status_t hs = HAL_I2C_Read(DS3231M_ADDR, DS3231M_REG_STATUS, &status_reg, 1);
    if (hs != HAL_OK) return hs;

    *lost_power = (status_reg & DS3231M_OSF_BIT) != 0;
    return HAL_OK;
}

hal_status_t HAL_RTC_ClearLostPowerFlag(void)
{
    if (!s_rtc_present) return HAL_ERR_INIT;

    /* Read-modify-write: register 0x0F also holds EN32kHz and the alarm
       flags (A1F/A2F), so a blind write of 0x00 would silently clear
       state this driver doesn't own. */
    uint8_t status_reg;
    hal_status_t hs = HAL_I2C_Read(DS3231M_ADDR, DS3231M_REG_STATUS, &status_reg, 1);
    if (hs != HAL_OK) return hs;

    status_reg &= (uint8_t)~DS3231M_OSF_BIT;
    return HAL_I2C_Write(DS3231M_ADDR, DS3231M_REG_STATUS, &status_reg, 1);
}

hal_status_t HAL_RTC_Init(void)
{
    /* Probe: try to read seconds register */
    uint8_t sec;
    hal_status_t hs = HAL_I2C_Read(DS3231M_ADDR, 0x00, &sec, 1);
    if (hs != HAL_OK) {
        ESP_LOGW(TAG, "DS3231M not detected at 0x%02x", DS3231M_ADDR);
        s_rtc_present = false;
        return HAL_OK; /* soft-fail: system can run without RTC */
    }

    s_rtc_present = true;
    ESP_LOGI(TAG, "DS3231M ready");

    bool lost_power = false;
    if (HAL_RTC_HasLostPower(&lost_power) == HAL_OK && lost_power) {
        ESP_LOGW(TAG, "OSF set: RTC oscillator stopped since last check -- "
                      "stored time is not trustworthy until a fresh NTP sync runs");
    }

    return HAL_OK;
}

hal_status_t HAL_RTC_GetTime(hal_time_t *t)
{
    if (!t) return HAL_ERR_DEV;
    if (!s_rtc_present) return HAL_ERR_INIT;

    uint8_t buf[7];
    hal_status_t hs = HAL_I2C_Read(DS3231M_ADDR, 0x00, buf, 7);
    if (hs != HAL_OK) return hs;

    t->seconds = bcd2bin(buf[0]);
    t->minutes = bcd2bin(buf[1]);
    t->hours   = bcd2bin(buf[2] & 0x3F); /* 24h mode mask */
    /* buf[3] is DOW; read but discarded -- see hal_rtc.h. */
    t->day     = bcd2bin(buf[4]);
    t->month   = bcd2bin(buf[5] & 0x1F);
    t->year    = (uint16_t)(2000 + bcd2bin(buf[6]));

    return HAL_OK;
}

hal_status_t HAL_RTC_SetTime(const hal_time_t *t)
{
    if (!t) return HAL_ERR_DEV;
    if (!s_rtc_present) return HAL_ERR_INIT;

    /* Validate every field before writing. bin2bcd() on an out-of-range
       value produces a nonsense BCD byte that the chip will happily
       latch -- a wrong time that looks plausible on read-back is worse
       than a rejected write. Ranges match the DS3231M datasheet. */
    if (t->seconds > 59 || t->minutes > 59 || t->hours > 23) {
        ESP_LOGE(TAG, "SetTime: bad h/m/s %u:%u:%u",
                 (unsigned)t->hours, (unsigned)t->minutes, (unsigned)t->seconds);
        return HAL_ERR_DEV;
    }
    if (t->day < 1 || t->day > 31 || t->month < 1 || t->month > 12) {
        ESP_LOGE(TAG, "SetTime: bad date d/m %u/%u",
                 (unsigned)t->day, (unsigned)t->month);
        return HAL_ERR_DEV;
    }
    if (t->year < 2000 || t->year > 2099) {
        /* DS3231M stores a 2-digit year; driver maps 2000..2099. */
        ESP_LOGE(TAG, "SetTime: year %u out of 2000..2099 range",
                 (unsigned)t->year);
        return HAL_ERR_DEV;
    }

    uint8_t buf[7];
    buf[0] = bin2bcd(t->seconds);
    buf[1] = bin2bcd(t->minutes);
    buf[2] = bin2bcd(t->hours);
    buf[3] = dow_from_ymd(t->year, t->month, t->day);  /* 1..7, ISO 8601 */
    buf[4] = bin2bcd(t->day);
    buf[5] = bin2bcd(t->month);
    buf[6] = bin2bcd((uint8_t)(t->year - 2000));

    hal_status_t hs = HAL_I2C_Write(DS3231M_ADDR, 0x00, buf, 7);
    if (hs != HAL_OK) return hs;

    /* We just wrote a (presumably correct) time, so the oscillator is
       running from a known-good starting point -- clear OSF so the next
       boot doesn't warn about a problem that's already been fixed.
       Non-fatal if this fails: the time write itself already succeeded. */
    if (HAL_RTC_ClearLostPowerFlag() != HAL_OK) {
        ESP_LOGW(TAG, "Time set OK, but failed to clear OSF flag");
    }

    return HAL_OK;
}