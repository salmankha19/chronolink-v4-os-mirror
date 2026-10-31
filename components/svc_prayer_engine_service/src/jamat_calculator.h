/*
 * jamat_calculator.h
 *
 * ChronoLink V4 OS -- Jamat tier calculator.
 *
 * INTERNAL to svc_prayer_engine_service (src/ is a PRIV_INCLUDE_DIRS
 * path). Not part of the published API.
 *
 * Pure arithmetic: season selection, anchor selection, rounding. It does
 * NOT read the timetable, does NOT log, and does NOT own the cache, the
 * event bus, or the jamat-vs-file trust decision -- those stay in the
 * service. This is the jamat half of the split described in
 * docs/prayer-times-model.md section 8.1.
 *
 * CONTRACT -- `adhan` must hold the EFFECTIVE adhan tier, i.e. after the
 * timetable merge and the tripwire, not raw engine output:
 *   adhan->adhan_sec[0..4]  : effective Fajr/Dhuhr/Asr/Maghrib/Isha
 *   adhan->raw.sunrise_sec  : effective sunrise  (Fajr SUNRISE_MINUS anchor)
 *   adhan->raw.midnight_sec : effective midnight (Isha MIDNIGHT_MINUS anchor)
 * The service fills these before calling.
 */
#pragma once
#include <stdint.h>
#include "svc_config.h"
#include "adhan_calculator.h"

uint8_t jamat_calculator_season_for_date(
    const svc_config_t *cfg,
    uint32_t date_yyyymmdd
);

void jamat_calculator_run(
    const svc_config_t *cfg,
    uint32_t date_yyyymmdd,
    const adhan_result_t *adhan,
    uint32_t jamat_sec_out[5]
);
