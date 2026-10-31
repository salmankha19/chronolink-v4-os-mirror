/*
 * test_jamat_differential.cpp
 *
 * Host differential test for the jamat half of svc_prayer_engine_service
 * (components/svc_prayer_engine_service/src/jamat_calculator.c).
 *
 * Why this exists: jamat is NOT covered by the on-device harness in
 * test_harness/, so this test is its gate. It pits the module against the
 * algorithm it replaced over every (config, date, effective adhan tier)
 * tuple that can change an answer.
 *
 *   left  : the four statics that used to live inline in
 *           svc_prayer_engine_service.c, copied verbatim from
 *           84144ae:components/svc_prayer_engine_service/src/
 *           svc_prayer_engine_service.c (the commit before the split),
 *           renamed old_* so both halves fit in one translation unit.
 *   right : jamat_calculator_run() and jamat_calculator_season_for_date(),
 *           linked from the component source (see the Makefile).
 *
 * Pre-refactor jamat algorithm verified against 84144ae on 2026-10-31.
 *
 * CONTRACT (docs/prayer-times-model.md section 8.1): jamat_calculator takes
 * the EFFECTIVE adhan tier -- after the timetable merge and after the
 * tripwire -- not raw engine output. That is exactly what the old inline
 * today-path loop consumed: 84144ae merges at 417-421, trips at 442-460,
 * builds adhan_secs[] from the surviving result.*_sec at 469-472, then
 * calls compute_jamat at 489-494. So eff_adhan[]/sunrise_eff/midnight_eff
 * are INPUTS here; the merge, the tripwire, merge_jamat(), next_sec[] and
 * the season field all stay in the service and are out of scope.
 *
 * Build/run:
 *   make -C prayer_host_tests test_jamat_differential
 *   ./prayer_host_tests/build/test_jamat_differential     (exit 0 = pass)
 *
 * No ESP-IDF, no hardware: svc_config.h needs only esp_err.h, which
 * prayer_host_tests/stubs/ supplies.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* C linkage: jamat_calculator.o is compiled as C by $(CC) (see the
 * Makefile), so the declarations this translation unit sees must not be
 * name-mangled. The standard headers above are pulled in first, so the
 * block below only sees project headers (svc_prayer_engine.h carries its
 * own extern "C" guard; jamat_calculator.h does not, and it is firmware
 * code -- not touched for a host test). */
extern "C" {
#include "jamat_calculator.h"
}

/* ------------- old implementation (verbatim from 84144ae) ------------- */

static bool old_date_in_range(uint8_t m, uint8_t d,
                              uint8_t start_m, uint8_t start_d,
                              uint8_t end_m, uint8_t end_d)
{
    uint16_t v = (uint16_t)m * 100u + d;
    uint16_t s = (uint16_t)start_m * 100u + start_d;
    uint16_t e = (uint16_t)end_m   * 100u + end_d;
    if (s <= e) return v >= s && v < e;
    return v >= s || v < e;
}

static uint8_t old_current_season(const season_cfg_t *s, uint8_t m, uint8_t d)
{
    return old_date_in_range(m, d, s->summer_start_month, s->summer_start_day,
                             s->winter_start_month, s->winter_start_day) ? 1u : 0u;
}

static uint32_t old_round_to_min(uint32_t sec, uint8_t round_min)
{
    if (round_min == 0) return sec;
    uint32_t step = (uint32_t)round_min * 60u;
    uint32_t half = step / 2u;
    return ((sec + half) / step) * step;
}

static uint32_t old_compute_jamat(uint32_t adhan_sec,
                                  uint32_t sunrise_sec,
                                  uint32_t midnight_sec,
                                  const jamat_season_cfg_t *cfg,
                                  uint8_t round_min)
{
    if (cfg->anchor == SVC_CONFIG_JAMAT_DISABLED) return UINT32_MAX;

    uint32_t base;
    uint32_t off_s = (uint32_t)cfg->offset_min * 60u;

    switch (cfg->anchor) {
    case SVC_CONFIG_JAMAT_ADHAN_PLUS:
        base = adhan_sec + off_s;
        break;
    case SVC_CONFIG_JAMAT_SUNRISE_MINUS:
        if (sunrise_sec == UINT32_MAX || sunrise_sec < off_s) return UINT32_MAX;
        base = sunrise_sec - off_s;
        break;
    case SVC_CONFIG_JAMAT_MIDNIGHT_MINUS:
        if (midnight_sec == UINT32_MAX || midnight_sec < off_s) return UINT32_MAX;
        base = midnight_sec - off_s;
        break;
    default:
        return UINT32_MAX;
    }
    return old_round_to_min(base, round_min);
}

/* The old today-path: season from the local date, then per-prayer compute
   against the EFFECTIVE (post-merge, post-tripwire) adhan tier. The date
   arrives as yyyymmdd exactly as the old loop derived (m, d) from
   snap.local; the arithmetic is unchanged. */
static void old_run(const svc_config_t *cfg, uint32_t date_yyyymmdd,
                    const uint32_t eff_adhan[5],
                    uint32_t sunrise_eff, uint32_t midnight_eff,
                    uint32_t out[5], uint8_t *season_out)
{
    uint8_t m = (uint8_t)((date_yyyymmdd / 100U) % 100U);
    uint8_t d = (uint8_t)(date_yyyymmdd % 100U);
    uint8_t season = old_current_season(&cfg->season, m, d);
    *season_out = season;

    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        const jamat_season_cfg_t *jc = (season == 0)
            ? &cfg->jamat[p].winter : &cfg->jamat[p].summer;
        out[p] = old_compute_jamat(eff_adhan[p], sunrise_eff, midnight_eff,
                                   jc, cfg->jamat[p].round_min);
    }
}

/* --------------------------- new implementation --------------------- */

/* The new path, filled exactly as svc_prayer_engine_service.c does it:
 * adhan_sec[] carries the effective tier, raw.sunrise_sec / raw.midnight_sec
 * carry the effective auxiliary values the jamat anchors subtract from. */
static void new_run(const svc_config_t *cfg, uint32_t date_yyyymmdd,
                    const uint32_t eff_adhan[5],
                    uint32_t sunrise_eff, uint32_t midnight_eff,
                    uint32_t out[5], uint8_t *season_out)
{
    adhan_result_t adhan;
    memset(&adhan, 0, sizeof(adhan));
    memcpy(adhan.adhan_sec, eff_adhan, sizeof(adhan.adhan_sec));
    adhan.raw.sunrise_sec  = sunrise_eff;
    adhan.raw.midnight_sec = midnight_eff;

    *season_out = jamat_calculator_season_for_date(cfg, date_yyyymmdd);
    jamat_calculator_run(cfg, date_yyyymmdd, &adhan, out);
}

/* ------------------------------ driver ----------------------------- */

static int bad = 0;         /* mirrors test_scheduler.cpp */
static long checks = 0;

#define CHECK(cond, msg) do {                               \
        if (!(cond)) { printf("FAIL: %s\n", msg); bad++; }   \
    } while (0)

/* Deterministic linear congruential generator (Numerical Recipes /
 * ISO-C-libc constants). Deliberately NOT std::mt19937: its seed expansion
 * and distribution adaptors are libstdc++-version dependent, so the sweep
 * would not be reproducible byte-for-byte across compilers. Same seed and
 * same call order as the session-5.8 differential, so the same 300,000
 * configs are replayed. */
static uint32_t rng_state = 0x12345678u;

static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static uint32_t rnd_range(uint32_t lo, uint32_t hi)
{
    return lo + (rnd() % (hi - lo + 1u));
}

/* Run both implementations on one tuple and report any divergence.
 * Every mismatch prints the full input so it can be reproduced by hand. */
static void compare(const svc_config_t *cfg, uint32_t date,
                    const uint32_t eff[5], uint32_t sunrise, uint32_t midnight,
                    const char *label)
{
    uint32_t a[5], b[5];
    uint8_t sa = 0xFF, sb = 0xFF;

    old_run(cfg, date, eff, sunrise, midnight, a, &sa);
    new_run(cfg, date, eff, sunrise, midnight, b, &sb);
    checks++;

    if (sa != sb) {
        printf("FAIL season [%s] date=%u old=%u new=%u\n",
               label, (unsigned)date, (unsigned)sa, (unsigned)sb);
        bad++;
    }
    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        if (a[p] != b[p]) {
            printf("FAIL jamat [%s] date=%u p=%d eff=%u sunrise=%u midnight=%u"
                   " anchor(w/s)=%u/%u off_w=%u round=%u :: old=%u new=%u\n",
                   label, (unsigned)date, p, (unsigned)eff[p],
                   (unsigned)sunrise, (unsigned)midnight,
                   (unsigned)cfg->jamat[p].winter.anchor,
                   (unsigned)cfg->jamat[p].summer.anchor,
                   (unsigned)cfg->jamat[p].winter.offset_min,
                   (unsigned)cfg->jamat[p].round_min,
                   (unsigned)a[p], (unsigned)b[p]);
            bad++;
        }
    }
}

/* ------------------------------- main ------------------------------ */

int main(void)
{
    static const uint8_t anchors[]  = { (uint8_t)SVC_CONFIG_JAMAT_DISABLED,
                                        (uint8_t)SVC_CONFIG_JAMAT_ADHAN_PLUS,
                                        (uint8_t)SVC_CONFIG_JAMAT_SUNRISE_MINUS,
                                        (uint8_t)SVC_CONFIG_JAMAT_MIDNIGHT_MINUS };
    static const uint16_t offsets[] = { 0, 1, 5, 10, 30, 60, 1440 };
    static const uint8_t rounds[]   = { 0, 1, 2, 5, 10, 15, 30 };

    /* 1. Exhaustive: every winter anchor x summer anchor x offset x rounding,
          on realistic and boundary dates (both season edges, leap day, both
          year ends).  7 dates * 4 * 4 * 7 * 7 = 5488 tuples. */
    static const uint32_t dates[] = { 20260615u, 20260115u, 20260329u,
                                      20261025u, 20261231u, 20260101u,
                                      20240229u };
    for (size_t di = 0; di < sizeof(dates) / sizeof(dates[0]); di++) {
        for (size_t w = 0; w < sizeof(anchors) / sizeof(anchors[0]); w++) {
            for (size_t s = 0; s < sizeof(anchors) / sizeof(anchors[0]); s++) {
                for (size_t o = 0; o < sizeof(offsets) / sizeof(offsets[0]); o++) {
                    for (size_t r = 0; r < sizeof(rounds) / sizeof(rounds[0]); r++) {
                        svc_config_t cfg;
                        memset(&cfg, 0, sizeof(cfg));
                        cfg.season.summer_start_month = 3;
                        cfg.season.summer_start_day   = 29;
                        cfg.season.winter_start_month = 10;
                        cfg.season.winter_start_day   = 25;
                        for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
                            cfg.jamat[p].winter.anchor     = anchors[w];
                            cfg.jamat[p].summer.anchor     = anchors[s];
                            cfg.jamat[p].winter.offset_min = offsets[o];
                            cfg.jamat[p].summer.offset_min = offsets[o];
                            cfg.jamat[p].round_min         = rounds[r];
                        }
                        uint32_t eff[5] = {
                            4u * 3600u + 125u,    /* ~04:02 fajr    */
                            13u * 3600u,          /* 13:00 dhuhr    */
                            17u * 3600u + 900u,   /* 17:15 asr      */
                            20u * 3600u + 2600u,  /* 20:43 maghrib  */
                            22u * 3600u + 3300u,  /* 22:55 isha     */
                        };
                        compare(&cfg, dates[di], eff,
                                4u * 3600u + 2950u,   /* sunrise  */
                                23u * 3600u + 4000u,  /* midnight */
                                "exhaustive");
                    }
                }
            }
        }
    }

    /* 2. Randomized sweep: random season config (including year-wrap, i.e.
          summer_start > winter_start), random dates, random effective tier,
          sentinel sunrise/midnight (UINT32_MAX), every anchor including one
          invalid value (4) in the winter slot. */
    for (long it = 0; it < 300000; it++) {
        svc_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.season.summer_start_month = (uint8_t)rnd_range(1, 12);
        cfg.season.summer_start_day   = (uint8_t)rnd_range(1, 28);
        cfg.season.winter_start_month = (uint8_t)rnd_range(1, 12);
        cfg.season.winter_start_day   = (uint8_t)rnd_range(1, 28);
        for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
            cfg.jamat[p].winter.anchor     = (uint8_t)rnd_range(0, 4); /* 4 = invalid */
            cfg.jamat[p].summer.anchor     = (uint8_t)rnd_range(0, 3);
            cfg.jamat[p].winter.offset_min = (uint16_t)rnd_range(0, 1440);
            cfg.jamat[p].summer.offset_min = (uint16_t)rnd_range(0, 1440);
            cfg.jamat[p].round_min         = (uint8_t)rnd_range(0, 30);
        }
        uint32_t date = (uint32_t)rnd_range(2020, 2100) * 10000u
                      + (uint32_t)rnd_range(1, 12) * 100u
                      + (uint32_t)rnd_range(1, 28);

        uint32_t eff[5];
        for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
            eff[p] = rnd_range(0, 90000);
        }
        uint32_t sunrise  = (rnd() & 1) ? UINT32_MAX : rnd_range(0, 90000);
        uint32_t midnight = (rnd() & 1) ? UINT32_MAX : rnd_range(0, 90000);

        compare(&cfg, date, eff, sunrise, midnight, "random");
    }

    /* 3. Guards on the module's entry points (no old counterpart: these
          return codes are new, so they are asserted directly). */
    {
        uint32_t out[5];
        adhan_result_t adhan;
        svc_config_t cfg;

        memset(&adhan, 0, sizeof(adhan));
        memset(&cfg, 0, sizeof(cfg));

        for (int p = 0; p < 5; p++) out[p] = 0xAAAA5555u;
        jamat_calculator_run(NULL, 20260615u, &adhan, out);
        for (int p = 0; p < 5; p++) {
            CHECK(out[p] == UINT32_MAX, "NULL cfg must yield UINT32_MAX");
        }

        for (int p = 0; p < 5; p++) out[p] = 0xAAAA5555u;
        jamat_calculator_run(&cfg, 20260615u, NULL, out);
        for (int p = 0; p < 5; p++) {
            CHECK(out[p] == UINT32_MAX, "NULL adhan must yield UINT32_MAX");
        }

        jamat_calculator_run(&cfg, 20260615u, &adhan, NULL);  /* must not crash */

        CHECK(jamat_calculator_season_for_date(NULL, 20260615u) == 0,
              "NULL cfg must report season 0");
        checks++;
    }

    printf("\n%ld comparisons, %d mismatches\n", checks, bad);
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
