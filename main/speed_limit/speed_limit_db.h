#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double longitude;
    double latitude;
    float bearing_deg;
    float accuracy_m;
    bool bearing_valid;
} speed_limit_position_t;

typedef struct {
    bool current_valid;
    uint16_t current_speed_kmh;
    bool next_valid;
    uint16_t next_speed_kmh;
    uint32_t next_distance_m;
    uint32_t current_distance_m;
    uint32_t current_lateral_m;
    uint16_t current_bearing_diff_deg;
    uint8_t confidence; // 0=none, 1=low, 2=medium, 3=high
} speed_limit_result_t;

// Validate the embedded SLDB database and reset tracking state.
esp_err_t speed_limit_db_init(void);

// Stateless map matching: every GPS fix is recalculated from the nearest
// direction-compatible speed points, so a stale limit is never latched.
bool speed_limit_db_update(const speed_limit_position_t *position,
                           speed_limit_result_t *result);

void speed_limit_db_reset(void);
uint32_t speed_limit_db_record_count(void);

#ifdef __cplusplus
}
#endif
