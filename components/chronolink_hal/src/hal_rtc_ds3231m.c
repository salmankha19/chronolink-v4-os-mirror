/*
 * hal_rtc_ds3231m.c
 *
 * ChronoLink V4 OS — DS3231M RTC Driver (I2C)
 *
 * Address: 0x68
 * Time registers: 0x00-0x06 (BCD)
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#include "hal_rtc.h"
#include "hal_i2c.h"
#include "esp_log.h"

static const char *TAG = "HAL_RTC";
#define DS3231M_ADDR 0x68

static inline uint8_t bcd2bin(uint8_t bcd)
{
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

static inline uint8_t bin2bcd(uint8_t bin)
{
    return ((bin / 10) << 4) | (bin % 10);
}

hal_status_t HAL_RTC_Init(void)
{
    /* Probe: try to read seconds register */
    uint8_t sec;
    hal_status_t hs = HAL_I2C_Read(DS3231M_ADDR, 0x00, &sec, 1);
    if (hs != HAL_OK) {
        ESP_LOGW(TAG, "DS3231M not detected at 0x%02x", DS3231M_ADDR);
    } else {
        ESP_LOGI(TAG, "DS3231M ready");
    }
    return HAL_OK; /* soft-fail: system can run without RTC */
}

hal_status_t HAL_RTC_GetTime(hal_time_t *t)
{
    if (!t) return HAL_ERR_DEV;

    uint8_t buf[7];
    hal_status_t hs = HAL_I2C_Read(DS3231M_ADDR, 0x00, buf, 7);
    if (hs != HAL_OK) return hs;

    t->seconds = bcd2bin(buf[0]);
    t->minutes = bcd2bin(buf[1]);
    t->hours   = bcd2bin(buf[2] & 0x3F); /* 24h mode mask */
    t->day     = bcd2bin(buf[4]);
    t->month   = bcd2bin(buf[5] & 0x1F);
    t->year    = (uint16_t)(2000 + bcd2bin(buf[6]));

    return HAL_OK;
}

hal_status_t HAL_RTC_SetTime(const hal_time_t *t)
{
    if (!t) return HAL_ERR_DEV;

    uint8_t buf[7];
    buf[0] = bin2bcd(t->seconds);
    buf[1] = bin2bcd(t->minutes);
    buf[2] = bin2bcd(t->hours);
    buf[3] = 0; /* day-of-week not used */
    buf[4] = bin2bcd(t->day);
    buf[5] = bin2bcd(t->month);
    buf[6] = bin2bcd((uint8_t)(t->year - 2000));

    return HAL_I2C_Write(DS3231M_ADDR, 0x00, buf, 7);
}