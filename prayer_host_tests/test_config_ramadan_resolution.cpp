// Host test for svc_config_is_ramadan().
//
// Why this exists: the "user override REPLACES the confirmed KSA table for
// any year it touches" semantic is the single place where a narrow
// override vs a wide table can silently go wrong. A mosque that publishes
// Feb 17 - Mar 18 2026 must get exactly that window, not the union with
// the KSA Feb 18 - Mar 19 window. The critical assertion below is that
// Mar 19 is FALSE when a narrowed 2026 override is present, even though
// the confirmed table says Mar 19 is still Ramadan.
//
// Links against svc_config_ramadan_resolution.o (compiled as C) and
// hijri_calendar.o (also C, already built for the hijri test). No NVS,
// no FreeRTOS, no locks: svc_config_is_ramadan() is pure.

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

extern "C" {
#include "svc_config.h"
}

static int bad = 0;

#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s", msg); putchar(10); bad++; } } while (0)

// Zero-initialised cfg: every field zeroed, ramadan_window_count implicitly 0.
// Returning svc_config_t by value is safe: it is a POD struct, and every
// modern g++ applies RVO.
static svc_config_t make_cfg_no_overrides()
{
    svc_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    return cfg;
}

static svc_config_ramadan_window_t make_window(
    uint16_t sy, uint8_t sm, uint8_t sd,
    uint16_t ey, uint8_t em, uint8_t ed)
{
    svc_config_ramadan_window_t w;
    w.start_year = sy; w.start_month = sm; w.start_day = sd;
    w.end_year = ey;   w.end_month = em;   w.end_day = ed;
    return w;
}

static void test_null(void)
{
    CHECK(!svc_config_is_ramadan(nullptr, 2026, 3, 1), "NULL cfg is false");
}

static void test_no_overrides_uses_table(void)
{
    svc_config_t cfg = make_cfg_no_overrides();

    // 2026 confirmed KSA window: Feb 18 - Mar 19.
    CHECK(!svc_config_is_ramadan(&cfg, 2026, 2, 17), "2026-02-17 not Ramadan");
    CHECK( svc_config_is_ramadan(&cfg, 2026, 2, 18), "2026-02-18 first day");
    CHECK( svc_config_is_ramadan(&cfg, 2026, 3, 19), "2026-03-19 last day");
    CHECK(!svc_config_is_ramadan(&cfg, 2026, 3, 20), "2026-03-20 Eid");

    // 2027 confirmed window: Feb 8 - Mar 8.
    CHECK( svc_config_is_ramadan(&cfg, 2027, 2, 8), "2027-02-08 first day");
    CHECK( svc_config_is_ramadan(&cfg, 2027, 3, 8), "2027-03-08 last day");
    CHECK(!svc_config_is_ramadan(&cfg, 2027, 3, 9), "2027-03-09 Eid");

    // 2025 not in table.
    CHECK(!svc_config_is_ramadan(&cfg, 2025, 3, 1), "2025 not in table");
}

static void test_override_replaces_table(void)
{
    // User narrows 2026 to Feb 17 - Mar 18. The critical case is Mar 19:
    // table says Ramadan, override says NO, and override wins.
    svc_config_t cfg = make_cfg_no_overrides();
    cfg.ramadan_windows[0] = make_window(2026, 2, 17, 2026, 3, 18);
    cfg.ramadan_window_count = 1;

    CHECK(!svc_config_is_ramadan(&cfg, 2026, 2, 16), "2026-02-16 before override start");
    CHECK( svc_config_is_ramadan(&cfg, 2026, 2, 17), "2026-02-17 override start (one day earlier than table)");
    CHECK( svc_config_is_ramadan(&cfg, 2026, 2, 18), "2026-02-18 both agree");
    CHECK( svc_config_is_ramadan(&cfg, 2026, 3, 18), "2026-03-18 override end");
    CHECK(!svc_config_is_ramadan(&cfg, 2026, 3, 19), "2026-03-19 override ends, table does not win");
    CHECK(!svc_config_is_ramadan(&cfg, 2026, 3, 20), "2026-03-20 Eid");
}

static void test_override_isolates_from_other_years(void)
{
    svc_config_t cfg = make_cfg_no_overrides();
    cfg.ramadan_windows[0] = make_window(2030, 1, 5, 2030, 2, 4);
    cfg.ramadan_window_count = 1;

    CHECK(!svc_config_is_ramadan(&cfg, 2030, 1, 4), "2030-01-04 before override");
    CHECK( svc_config_is_ramadan(&cfg, 2030, 1, 5), "2030-01-05 override start");
    CHECK( svc_config_is_ramadan(&cfg, 2030, 2, 4), "2030-02-04 override end");
    CHECK(!svc_config_is_ramadan(&cfg, 2030, 2, 5), "2030-02-05 after override");

    // 2026 untouched -> uses table.
    CHECK( svc_config_is_ramadan(&cfg, 2026, 3, 1),  "2026-03-01 uses table (untouched by 2030 override)");
    CHECK( svc_config_is_ramadan(&cfg, 2026, 3, 19), "2026-03-19 uses table");
}

static void test_cross_year_override(void)
{
    svc_config_t cfg = make_cfg_no_overrides();
    cfg.ramadan_windows[0] = make_window(2028, 12, 15, 2029, 1, 15);
    cfg.ramadan_window_count = 1;

    CHECK(!svc_config_is_ramadan(&cfg, 2028, 12, 14), "2028-12-14 before");
    CHECK( svc_config_is_ramadan(&cfg, 2028, 12, 15), "2028-12-15 start");
    CHECK( svc_config_is_ramadan(&cfg, 2028, 12, 31), "2028-12-31 in-window");
    CHECK( svc_config_is_ramadan(&cfg, 2029, 1, 1),   "2029-01-01 in-window");
    CHECK( svc_config_is_ramadan(&cfg, 2029, 1, 15),  "2029-01-15 end");
    CHECK(!svc_config_is_ramadan(&cfg, 2029, 1, 16),  "2029-01-16 after");

    // 2026 unaffected.
    CHECK( svc_config_is_ramadan(&cfg, 2026, 3, 1), "2026-03-01 uses table");
}

static void test_multi_window(void)
{
    svc_config_t cfg = make_cfg_no_overrides();
    cfg.ramadan_windows[0] = make_window(2028, 2, 15, 2028, 3, 15);
    cfg.ramadan_windows[1] = make_window(2030, 1, 5, 2030, 2, 4);
    cfg.ramadan_window_count = 2;

    CHECK( svc_config_is_ramadan(&cfg, 2028, 3, 1),  "2028 in first override");
    CHECK( svc_config_is_ramadan(&cfg, 2030, 1, 20), "2030 in second override");
    CHECK(!svc_config_is_ramadan(&cfg, 2029, 3, 1),  "2029 (between them) not in table");
    CHECK( svc_config_is_ramadan(&cfg, 2026, 3, 1),  "2026 uses table");
}

int main()
{
    printf("=== svc_config_is_ramadan (resolution) ===\n");
    test_null();
    test_no_overrides_uses_table();
    test_override_replaces_table();
    test_override_isolates_from_other_years();
    test_cross_year_override();
    test_multi_window();
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
