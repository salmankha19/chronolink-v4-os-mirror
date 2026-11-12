// Host test for svc_config_ramadan_window_is_valid().
//
// Why this exists: a bad override window either shifts Isha by 30 min for
// the wrong day range, or lets a user persist an inverted/leap-year-broken
// window that the service will then silently skip every day. The validator
// is the only thing standing between the user's JSON and NVS.
//
// Pure C host test: no ESP-IDF, no config, no NVS mock. Links against
// ramadan_window_validator.o compiled as C (see the Makefile edit in 5a.4).

#include <stdio.h>
#include <stdbool.h>

extern "C" {
#include "ramadan_window_validator.h"
}

static int bad = 0;

#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s", msg); putchar(10); bad++; } } while (0)

static void test_same_year_valid(void)
{
    CHECK(svc_config_ramadan_window_is_valid(2028, 2, 15, 2028, 3, 15),
          "same-year normal window");
    CHECK(svc_config_ramadan_window_is_valid(2028, 1, 1, 2028, 12, 31),
          "same-year full-year window");
    CHECK(svc_config_ramadan_window_is_valid(2028, 3, 15, 2028, 3, 15),
          "single-day window (start == end)");
    CHECK(svc_config_ramadan_window_is_valid(2028, 3, 15, 2028, 3, 16),
          "adjacent-day window");
}

static void test_same_year_invalid(void)
{
    CHECK(!svc_config_ramadan_window_is_valid(2028, 3, 15, 2028, 2, 15),
          "same-year inverted window (end < start)");
    CHECK(!svc_config_ramadan_window_is_valid(2028, 12, 31, 2028, 1, 1),
          "same-year Dec-31 to Jan-1 is inverted");
}

static void test_cross_year_valid(void)
{
    CHECK(svc_config_ramadan_window_is_valid(2028, 12, 15, 2029, 1, 15),
          "cross-year normal window");
    CHECK(svc_config_ramadan_window_is_valid(2028, 6, 15, 2029, 6, 14),
          "cross-year near-anniversary window");
    CHECK(svc_config_ramadan_window_is_valid(2028, 1, 1, 2029, 12, 31),
          "cross-year two-year window");
    CHECK(svc_config_ramadan_window_is_valid(2028, 12, 31, 2029, 1, 1),
          "cross-year single-day window");
}

static void test_cross_year_invalid(void)
{
    CHECK(!svc_config_ramadan_window_is_valid(2029, 1, 15, 2028, 12, 15),
          "end year before start year rejected");
    CHECK(!svc_config_ramadan_window_is_valid(2028, 1, 1, 2030, 1, 1),
          "two-year gap rejected (end_year > start_year + 1)");
    CHECK(!svc_config_ramadan_window_is_valid(2028, 1, 1, 2035, 12, 31),
          "seven-year window rejected");
}

static void test_invalid_ranges(void)
{
    CHECK(!svc_config_ramadan_window_is_valid(1899, 1, 1, 1899, 1, 15),
          "year 1899 is out of range");
    CHECK(!svc_config_ramadan_window_is_valid(2028, 0, 1, 2028, 1, 1),
          "month 0 rejected");
    CHECK(!svc_config_ramadan_window_is_valid(2028, 13, 1, 2028, 12, 31),
          "month 13 rejected");
    CHECK(!svc_config_ramadan_window_is_valid(2028, 1, 0, 2028, 1, 15),
          "day 0 rejected");
    CHECK(!svc_config_ramadan_window_is_valid(2028, 1, 32, 2028, 2, 1),
          "day 32 rejected");
    CHECK(!svc_config_ramadan_window_is_valid(2028, 4, 31, 2028, 5, 1),
          "April 31 does not exist");
    CHECK(!svc_config_ramadan_window_is_valid(2028, 2, 30, 2028, 3, 1),
          "Feb 30 does not exist");
    CHECK(!svc_config_ramadan_window_is_valid(2101, 1, 1, 2101, 1, 15),
          "year 2101 is out of range");
}

static void test_leap_year(void)
{
    CHECK(svc_config_ramadan_window_is_valid(2028, 2, 29, 2028, 3, 1),
          "2028 is leap, Feb 29 valid");
    CHECK(!svc_config_ramadan_window_is_valid(2027, 2, 29, 2027, 3, 1),
          "2027 is not leap, Feb 29 rejected");
    CHECK(svc_config_ramadan_window_is_valid(2000, 2, 29, 2000, 3, 1),
          "2000 is leap (400-divisible), Feb 29 valid");
    CHECK(!svc_config_ramadan_window_is_valid(1900, 2, 29, 1900, 3, 1),
          "1900 is not leap (100-divisible not 400), Feb 29 rejected");
    CHECK(!svc_config_ramadan_window_is_valid(2100, 2, 29, 2100, 3, 1),
          "2100 is not leap, Feb 29 rejected");
}

int main()
{
    printf("=== ramadan window validator ===\n");
    test_same_year_valid();
    test_same_year_invalid();
    test_cross_year_valid();
    test_cross_year_invalid();
    test_invalid_ranges();
    test_leap_year();
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
