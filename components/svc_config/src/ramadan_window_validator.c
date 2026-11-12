// ramadan_window_validator.c
//
// See ramadan_window_validator.h for the contract.
//
// Pure C, no ESP-IDF dependency, no allocation, no logging.

#include "ramadan_window_validator.h"

// Days in the given month, leap-year aware. Returns 0 for invalid month.
static int days_in_month(int year, int month)
{
    static const int d[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    if (month == 2) {
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return d[month - 1];
}

static bool date_valid(int year, int month, int day)
{
    if (year < 1900 || year > 2100) return false;
    int dim = days_in_month(year, month);
    if (dim == 0) return false;
    return day >= 1 && day <= dim;
}

static int yyyymmdd(int year, int month, int day)
{
    return year * 10000 + month * 100 + day;
}

bool svc_config_ramadan_window_is_valid(int start_year, int start_month, int start_day,
                                        int end_year, int end_month, int end_day)
{
    if (!date_valid(start_year, start_month, start_day)) return false;
    if (!date_valid(end_year,   end_month,   end_day))   return false;

    if (end_year < start_year) return false;
    if (end_year > start_year + 1) return false;

    if (end_year == start_year) {
        int s = yyyymmdd(start_year, start_month, start_day);
        int e = yyyymmdd(end_year,   end_month,   end_day);
        return e >= s;
    }

    // end_year == start_year + 1: any valid dates form a coherent
    // cross-year window (start_year-12-31 -> end_year-01-01 is the
    // shortest possible; start_year-01-01 -> end_year-12-31 is the
    // longest). Both are accepted; the caller owns the window length.
    return true;
}
