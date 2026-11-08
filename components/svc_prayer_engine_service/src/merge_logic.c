/*
 * merge_logic.c
 *
 * ChronoLink V4 OS -- Timetable-vs-computed merge logic (implementation).
 *
 * No ESP-IDF dependencies; compiles on host with plain gcc. See
 * merge_logic.h for the contract.
 */
#include "merge_logic.h"

uint32_t merge_logic_jamat_resolve(bool has_file, uint32_t file_val,
                                   uint32_t computed_val, uint32_t adhan_val,
                                   uint32_t next_val,
                                   merge_jamat_action_t *action)
{
    merge_jamat_action_t a = MERGE_JAMAT_ACCEPTED;
    uint32_t out;

    if (!has_file) {
        a   = MERGE_JAMAT_NO_FILE;
        out = computed_val;
    } else if (file_val < adhan_val) {
        a   = MERGE_JAMAT_BELOW_ADHAN;
        out = computed_val;
    } else {
        if (next_val != 0 && file_val >= next_val) {
            a = MERGE_JAMAT_ABOVE_NEXT;
        }
        out = file_val;
    }

    if (action) *action = a;
    return out;
}
