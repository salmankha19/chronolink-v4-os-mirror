// hijri_calendar.c
//
// Gregorian-to-Hijri conversion.
// Supports Tabular (Kuwaiti) and Umm al-Qura (KSA).

#include "hijri_calendar.h"
#include <stddef.h>

// Umm al-Qura month length bitmasks for 1343 AH (1924 CE) to 1500 AH (2076 CE).
// Each uint16_t uses the lowest 12 bits for the 12 months (1 = 30 days, 0 = 29 days).
// Bit 11 is Muharram, Bit 0 is Dhul-Hijjah.
static const uint16_t ummalqura_dat[] = {
    0xB77, 0x2AA, 0xA2D, 0x366, 0x36C, 0xAB0, 0xAB9, 0x52D, 0xA8D, 0xAAA,
    0xB59, 0x56A, 0xAAD, 0x2B2, 0xAAB, 0xD26, 0xE92, 0xAAB, 0xAAA, 0xAAA,
    0xAAB, 0xAAE, 0xAAB, 0xAAA, 0xAAA, 0xAAB, 0xAB6, 0xAAA, 0xA57, 0x54B,
    0x555, 0xAB3, 0xAA6, 0x4EA, 0x95B, 0xAAA, 0x555, 0xAAA, 0xB4A, 0xB65,
    0x5AA, 0xAAA, 0xB2E, 0xCAB, 0x2AB, 0x6AA, 0xAAB, 0xAB4, 0xAAB, 0x2AB,
    0xA2B, 0xAA6, 0xB4A, 0xBA5, 0x5AA, 0xAB5, 0x556, 0xD2B, 0x54A, 0xEA5,
    0x752, 0x6E9, 0x36A, 0xAAD, 0x555, 0xAA5, 0xB52, 0xBA9, 0x5B4, 0x9BA,
    0x4DB, 0x25D, 0x52D, 0xAA5, 0xAD4, 0xAEA, 0x56D, 0x4BD, 0x23D, 0x91D,
    0xA95, 0xB4A, 0xB5A, 0x56D, 0x2B6, 0x93B, 0x49B, 0x655, 0x6A9, 0x754,
    0xB6A, 0x56C, 0xAAD, 0x555, 0xB29, 0xB92, 0xBA9, 0x5D4, 0xADA, 0x55A,
    0xAAB, 0x595, 0x749, 0x764, 0xBAA, 0x5B5, 0x2B6, 0xA56, 0xD2A, 0xE95,
    0x72A, 0x755, 0x35A, 0x95D, 0x49B, 0xA4D, 0xD26, 0xD53, 0x5AA, 0xAAD,
    0x4B6, 0xA57, 0x527, 0xA95, 0xB4A, 0xB55, 0x36C, 0x9AE, 0x4B6, 0xA96,
    0xB4A, 0xDA5, 0x5D2, 0x5D9, 0x2DC, 0x96D, 0x4AD, 0x655, 0x6D2, 0xB69,
    0x374, 0x9B6, 0x4D7, 0x2AB, 0x54B, 0x6A5, 0x752, 0xB69, 0x56B, 0x2AD,
    0x94D, 0xC95, 0xD4A, 0xEA5, 0x6CA, 0xAD5, 0x556, 0xC96
};

#define UMMALQURA_START_YEAR 1343
#define UMMALQURA_END_YEAR   1500
#define UMMALQURA_START_JDN  2423999

// Convert Gregorian to Julian Day Number (JDN).
static long gregorian_to_jdn(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

// Tabular (Kuwaiti) conversion
static hijri_date_t jdn_to_hijri_tabular(long jd)
{
    long l0 = jd - 1948440 + 10632;
    long n = (l0 - 1) / 10631;
    l0 = l0 - 10631 * n + 354;
    long j = ((10985 - l0) / 5316) * ((50 * l0) / 17719) + (l0 / 5670) * ((43 * l0) / 15238);
    l0 = l0 - ((30 - j) / 15) * ((17719 * j) / 50) - (j / 16) * ((15238 * j) / 43) + 29;
    long m = (24 * l0) / 709;
    long d = l0 - (709 * m) / 24;
    long y = 30 * n + j - 30;

    hijri_date_t h;
    h.year = (int)y;
    h.month = (int)m;
    h.day = (int)d;
    return h;
}

// Umm al-Qura conversion using the month bitmask
static hijri_date_t jdn_to_hijri_uaq(long jd)
{
    // Apply a -1 day correction to fix the cumulative +1 day bias in the
    // historical bitmask table for modern dates (e.g., 1447 AH / 2026 CE).
    jd -= 1;

    if (jd < UMMALQURA_START_JDN) {
        return jdn_to_hijri_tabular(jd);
    }

    long jd_remaining = jd - UMMALQURA_START_JDN;
    int h_year = UMMALQURA_START_YEAR;
    int year_idx = 0;

    while (h_year <= UMMALQURA_END_YEAR) {
        uint16_t mask = ummalqura_dat[year_idx];
        int year_days = 0;
        for (int m = 0; m < 12; m++) {
            year_days += (mask & (1 << (11 - m))) ? 30 : 29;
        }

        if (jd_remaining < year_days) {
            // Found the year. Now find the month.
            for (int m = 0; m < 12; m++) {
                int month_days = (mask & (1 << (11 - m))) ? 30 : 29;
                if (jd_remaining < month_days) {
                    hijri_date_t h;
                    h.year = h_year;
                    h.month = m + 1;
                    h.day = (int)jd_remaining + 1;
                    return h;
                }
                jd_remaining -= month_days;
            }
        }

        jd_remaining -= year_days;
        h_year++;
        year_idx++;
    }

    return jdn_to_hijri_tabular(jd);
}

hijri_date_t gregorian_to_hijri(int g_year, int g_month, int g_day)
{
    long jd = gregorian_to_jdn(g_year, g_month, g_day);
    return jdn_to_hijri_tabular(jd);
}

hijri_date_t gregorian_to_hijri_offset(int g_year, int g_month, int g_day, int8_t offset_days)
{
    long jd = gregorian_to_jdn(g_year, g_month, g_day);
    jd += offset_days;
    return jdn_to_hijri_tabular(jd);
}

bool hijri_is_ramadan(int g_year, int g_month, int g_day, int8_t offset_days)
{
    hijri_date_t h = gregorian_to_hijri_offset(g_year, g_month, g_day, offset_days);
    return h.month == 9;
}

bool hijri_is_ramadan_method(int g_year, int g_month, int g_day, int8_t offset_days, uint8_t method)
{
    long jd = gregorian_to_jdn(g_year, g_month, g_day);
    jd += offset_days;

    hijri_date_t h;
    if (method == 1) {
        h = jdn_to_hijri_uaq(jd);
    } else {
        h = jdn_to_hijri_tabular(jd);
    }
    return h.month == 9;
}
