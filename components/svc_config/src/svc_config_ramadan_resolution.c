// svc_config_ramadan_resolution.c
//
// ChronoLink V4 OS -- user override OR confirmed KSA table, resolved.
//
// Split out of svc_config.c so it can be host-tested in isolation: this
// file is pure -- no NVS, no locks, no allocation, no ESP-IDF deps beyond
// what svc_config.h already pulls in (esp_err.h, which prayer_host_tests/
// stubs/ supplies). Every other function in svc_config.c needs nvs.h /
// freertos/semphr.h / esp_log.h, which the host-test stubs tree does not
// supply, so anything testable lives in its own file.
//
// Resolution order (matches the svc_config.h doc block):
//   1. If any cfg->ramadan_windows[i] has start_year == year OR
//      end_year == year, the user owns that year: return true iff
//      (year, month, day) is inside some override window. NEVER fall
//      through to the table for that year.
//   2. Else fall through to uaq_is_ramadan() (the confirmed KSA table).
//   3. Else false.

#include "svc_config.h"
#include "hijri_calendar.h"

#include <stddef.h>

bool svc_config_is_ramadan(const svc_config_t *cfg, int year, int month, int day)
{
    if (!cfg) return false;

    int limit = cfg->ramadan_window_count;
    if (limit < 0) limit = 0;
    if (limit > SVC_CONFIG_RAMADAN_MAX) limit = SVC_CONFIG_RAMADAN_MAX;

    int target = year * 10000 + month * 100 + day;
    bool year_has_override = false;

    for (int i = 0; i < limit; i++) {
        const svc_config_ramadan_window_t *w = &cfg->ramadan_windows[i];

        // An override "covers" a year if it starts or ends in that year.
        // This also correctly handles a cross-year window (start_year Y,
        // end_year Y+1) against a query in either of the two years.
        if ((int)w->start_year == year || (int)w->end_year == year) {
            year_has_override = true;
        }

        int start = (int)w->start_year * 10000
                  + (int)w->start_month * 100
                  + (int)w->start_day;
        int end   = (int)w->end_year   * 10000
                  + (int)w->end_month   * 100
                  + (int)w->end_day;
        if (target >= start && target <= end) return true;
    }

    // Any override entry that mentions this year owns it. Do NOT fall
    // through to the table -- the user's narrower window must win.
    if (year_has_override) return false;

    // No override touches this year. Use the confirmed KSA table.
    return uaq_is_ramadan(year, month, day);
}
