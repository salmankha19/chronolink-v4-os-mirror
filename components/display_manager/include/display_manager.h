#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void display_manager_init(void);
bool display_manager_submit_json(const char *json, size_t len);
void display_manager_process_frame(void);

typedef struct {
    uint32_t frames_processed;
    uint32_t frames_dropped;
    uint32_t parse_errors;
} display_manager_stats_t;

display_manager_stats_t display_manager_get_stats(void);
