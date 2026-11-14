// hijri_calendar.c
//
// Algorithmic Gregorian-to-Hijri conversion (Kuwaiti algorithm).
//
// Pure C, no ESP-IDF dependency.

#include "hijri_calendar.h"
#include <stddef.h>

// Convert Gregorian to Julian Day Number (JDN).
static long gregorian_to_jdn(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

// Convert Julian Day Number to Hijri.
static hijri_date_t jdn_to_hijri(long jd)
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

hijri_date_t gregorian_to_hijri(int g_year, int g_month, int g_day)
{
    long jd = gregorian_to_jdn(g_year, g_month, g_day);
    return jdn_to_hijri(jd);
}

hijri_date_t gregorian_to_hijri_offset(int g_year, int g_month, int g_day, int8_t offset_days)
{
    long jd = gregorian_to_jdn(g_year, g_month, g_day);
    jd += offset_days;
    return jdn_to_hijri(jd);
}

bool hijri_is_ramadan(int g_year, int g_month, int g_day, int8_t offset_days)
{
    hijri_date_t h = gregorian_to_hijri_offset(g_year, g_month, g_day, offset_days);
    return h.month == 9;
}
