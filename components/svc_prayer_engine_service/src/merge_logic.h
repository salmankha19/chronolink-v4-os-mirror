/*
 * merge_logic.h
 *
 * ChronoLink V4 OS -- Timetable-vs-computed merge logic.
 *
 * INTERNAL to svc_prayer_engine_service (src/ is a PRIV_INCLUDE_DIRS
 * path). Not part of the published API.
 *
 * Pure arithmetic: decides whether a timetable value or a computed
 * value wins, without reading the timetable, without logging, and
 * without owning the cache or the event bus -- those stay in the
 * service. Extracted from svc_prayer_engine_service.c (#138 item (l))
 * so the decision rule can be unit-tested on the host without an
 * ESP-IDF toolchain or mocks.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MERGE_JAMAT_NO_FILE = 0,  /* !has_file: computed_val returned, no log */
    MERGE_JAMAT_BELOW_ADHAN,  /* file < adhan: computed_val returned, caller logs */
    MERGE_JAMAT_ABOVE_NEXT,   /* file >= next: file_val returned, caller logs */
    MERGE_JAMAT_ACCEPTED      /* file in [adhan, next): file_val returned, no log */
} merge_jamat_action_t;

/* Resolve a jamat value against its adhan and next-prayer bracket.
   Pure decision; *action (if non-NULL) reports which branch fired so
   the caller can preserve the two WARN paths that this module cannot
   own (no ESP-IDF deps here).

   Rule (unchanged from 5.8):
     !has_file                  -> computed_val
     file_val < adhan_val       -> computed_val (reject, fall back)
     file_val >= next_val != 0  -> file_val (accept with warn, mosque authority)
     otherwise                  -> file_val (accept silently) */
uint32_t merge_logic_jamat_resolve(bool has_file, uint32_t file_val,
                                   uint32_t computed_val, uint32_t adhan_val,
                                   uint32_t next_val,
                                   merge_jamat_action_t *action);

#ifdef __cplusplus
}
#endif
