/*
 * jamat_calculator.c
 *
 * ChronoLink V4 OS -- Jamat tier calculator (internal).
 *
 * The four helpers below are a literal move of the statics that used to
 * live in svc_prayer_engine_service.c (date_in_range, current_season,
 * round_to_min, compute_jamat). The arithmetic is byte-for-byte the same;
 * only current_season() changed shape, taking (cfg, yyyymmdd) so the
 * service can ask for result.season without duplicating the date maths.
 */
#include "jamat_calculator.h"

#include <stdbool.h>
#include <stdint.h>

/* True if (m, d) is in [start, end) with year-wrap. */
static bool date_in_range(uint8_t m, uint8_t d,
                          uint8_t start_m, uint8_t start_d,
                          uint8_t end_m, uint8_t end_d)
{
    uint16_t v = (uint16_t)m * 100u + d;
    uint16_t s = (uint16_t)start_m * 100u + start_d;
    uint16_t e = (uint16_t)end_m   * 100u + end_d;
    if (s <= e) return v >= s && v < e;
    return v >= s || v < e;
}

/* Round `sec` to the nearest multiple of `round_min * 60` seconds from
   local midnight. round_min == 0 means "return sec unchanged". */
static uint32_t round_to_min(uint32_t sec, uint8_t round_min)
{
    if (round_min == 0) return sec;
    uint32_t step = (uint32_t)round_min * 60u;
    uint32_t half = step / 2u;
    return ((sec + half) / step) * step;
}

/* Compute one prayer's jamat from its anchor value and cfg. Returns
   UINT32_MAX if the anchor is DISABLED or the target is invalid. */
static uint32_t compute_jamat(uint32_t adhan_sec,
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
    return round_to_min(base, round_min);
}

/* Summer (1) if (m, d) is in [summer_start, winter_start), else winter (0).
   Same rule the service used inline as current_season(). */
uint8_t jamat_calculator_season_for_date(const svc_config_t *cfg,
                                         uint32_t date_yyyymmdd)
{
    if (cfg == NULL) return 0;

    uint8_t m = (uint8_t)((date_yyyymmdd / 100U) % 100U);
    uint8_t d = (uint8_t)(date_yyyymmdd % 100U);
    const season_cfg_t *s = &cfg->season;

    return date_in_range(m, d,
                         s->summer_start_month, s->summer_start_day,
                         s->winter_start_month, s->winter_start_day) ? 1u : 0u;
}

/* Fill jamat_sec_out[5] with the COMPUTED jamat for each prayer (no
   timetable file values -- the service applies those). Entries are
   UINT32_MAX when the prayer's anchor is disabled or its target invalid. */
void jamat_calculator_run(const svc_config_t *cfg,
                          uint32_t date_yyyymmdd,
                          const adhan_result_t *adhan,
                          uint32_t jamat_sec_out[5])
{
    if (jamat_sec_out == NULL) return;

    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        jamat_sec_out[p] = UINT32_MAX;
    }
    if (cfg == NULL || adhan == NULL) return;

    uint8_t season = jamat_calculator_season_for_date(cfg, date_yyyymmdd);

    for (int p = 0; p < SVC_CONFIG_PRAYER_COUNT; p++) {
        const jamat_season_cfg_t *jc = (season == 0)
            ? &cfg->jamat[p].winter : &cfg->jamat[p].summer;

        jamat_sec_out[p] = compute_jamat(
            adhan->adhan_sec[p],
            adhan->raw.sunrise_sec,     /* effective sunrise  */
            adhan->raw.midnight_sec,    /* effective midnight */
            jc,
            cfg->jamat[p].round_min);
    }
}
