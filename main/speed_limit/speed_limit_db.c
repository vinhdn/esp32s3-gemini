#include "speed_limit_db.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"

#define SLDB_MAGIC 0x42444c53u /* "SLDB" little-endian */
#define SLDB_VERSION 1u
#define METERS_PER_DEGREE 111320.0
#define SEARCH_RADIUS_M 5000.0
#define MAX_BEARING_DIFF_DEG 60.0
#define MAX_CURRENT_DISTANCE_M 500.0
#define MAX_CURRENT_LATERAL_M 150.0
#define MAX_CURRENT_BEARING_DIFF_DEG 40.0
#define MAX_NO_BEARING_DISTANCE_M 150.0
#define MAX_NEXT_LATERAL_M 300.0
#define MAX_NEXT_BEARING_DIFF_DEG 45.0

// EMBED_FILES converts '/' and '.' to '_' in the linker symbol.
extern const uint8_t speed_limit_db_start[] asm("_binary_speed_limit_data_bin_start");
extern const uint8_t speed_limit_db_end[] asm("_binary_speed_limit_data_bin_end");

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint32_t record_count;
} db_header_t;

typedef struct __attribute__((packed)) {
    int32_t longitude_e6;
    int32_t latitude_e6;
    uint16_t bearing_deg;
    uint8_t speed_kmh;
    uint8_t reserved;
} db_record_t;

typedef struct {
    bool valid;
    const db_record_t *record;
    double distance_m;
    double along_m;
    double lateral_m;
    double bearing_diff_deg;
    double score;
} candidate_t;

static const char *TAG = "speed_limit_db";
static const db_header_t *s_header;
static const db_record_t *s_records;
static bool s_ready;

static double normalize_angle(double angle)
{
    while (angle < 0.0) angle += 360.0;
    while (angle >= 360.0) angle -= 360.0;
    return angle;
}

static double angle_difference(double a, double b)
{
    double diff = fabs(normalize_angle(a) - normalize_angle(b));
    return diff > 180.0 ? 360.0 - diff : diff;
}

static uint32_t lower_bound_latitude(int32_t latitude_e6)
{
    uint32_t lo = 0;
    uint32_t hi = s_header->record_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (s_records[mid].latitude_e6 < latitude_e6) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

void speed_limit_db_reset(void)
{
    // Lookup is intentionally stateless: every GPS fix is calculated again so
    // a stale speed can never remain latched when the vehicle changes road.
}

esp_err_t speed_limit_db_init(void)
{
    const size_t size = (size_t)(speed_limit_db_end - speed_limit_db_start);
    if (size < sizeof(db_header_t)) {
        ESP_LOGE(TAG, "Database missing or too small: %u bytes", (unsigned)size);
        return ESP_ERR_INVALID_SIZE;
    }

    s_header = (const db_header_t *)speed_limit_db_start;
    const size_t expected = sizeof(db_header_t) +
                            (size_t)s_header->record_count * sizeof(db_record_t);
    if (s_header->magic != SLDB_MAGIC || s_header->version != SLDB_VERSION ||
        s_header->record_size != sizeof(db_record_t) || expected != size) {
        ESP_LOGE(TAG,
                 "Invalid database: magic=%08lx version=%u record_size=%u count=%lu size=%u expected=%u",
                 (unsigned long)s_header->magic, s_header->version, s_header->record_size,
                 (unsigned long)s_header->record_count, (unsigned)size, (unsigned)expected);
        return ESP_ERR_INVALID_RESPONSE;
    }

    s_records = (const db_record_t *)(speed_limit_db_start + sizeof(db_header_t));
    s_ready = true;
    speed_limit_db_reset();
    ESP_LOGI(TAG, "Loaded %lu speed-limit points (%u bytes)",
             (unsigned long)s_header->record_count, (unsigned)size);
    return ESP_OK;
}

uint32_t speed_limit_db_record_count(void)
{
    return s_ready ? s_header->record_count : 0;
}

static void consider(candidate_t *best, const db_record_t *record,
                     double distance_m, double along_m, double lateral_m,
                     double bearing_diff_deg, double score)
{
    if (!best->valid || score < best->score) {
        best->valid = true;
        best->record = record;
        best->distance_m = distance_m;
        best->along_m = along_m;
        best->lateral_m = lateral_m;
        best->bearing_diff_deg = bearing_diff_deg;
        best->score = score;
    }
}

bool speed_limit_db_update(const speed_limit_position_t *position,
                           speed_limit_result_t *result)
{
    if (!s_ready || !position || !result ||
        position->latitude < -90.0 || position->latitude > 90.0 ||
        position->longitude < -180.0 || position->longitude > 180.0) {
        return false;
    }
    memset(result, 0, sizeof(*result));

    const double latitude_rad = position->latitude * (M_PI / 180.0);
    const double cos_latitude = cos(latitude_rad);
    const double heading_rad = normalize_angle(position->bearing_deg) * (M_PI / 180.0);
    const double heading_east = sin(heading_rad);
    const double heading_north = cos(heading_rad);
    const int32_t center_lat_e6 = (int32_t)llround(position->latitude * 1000000.0);
    const int32_t delta_lat_e6 = (int32_t)ceil((SEARCH_RADIUS_M / METERS_PER_DEGREE) * 1000000.0);
    const uint32_t begin = lower_bound_latitude(center_lat_e6 - delta_lat_e6);
    const uint32_t end = lower_bound_latitude(center_lat_e6 + delta_lat_e6 + 1);

    candidate_t nearest = {0};
    candidate_t passed = {0};

    // Recalculate from scratch for every fix. Bearing is the strongest road/
    // carriageway discriminator in this point-only dataset. Candidate scoring
    // remains tolerant of curves; a final confidence gate below rejects points
    // whose distance/lateral offset makes a parallel-road match too likely.
    for (uint32_t i = begin; i < end; ++i) {
        const db_record_t *record = &s_records[i];
        const double record_lat = (double)record->latitude_e6 / 1000000.0;
        const double record_lon = (double)record->longitude_e6 / 1000000.0;
        const double dx = (record_lon - position->longitude) * METERS_PER_DEGREE * cos_latitude;
        const double dy = (record_lat - position->latitude) * METERS_PER_DEGREE;
        const double distance = sqrt(dx * dx + dy * dy);
        if (distance > SEARCH_RADIUS_M) continue;

        double bearing_diff = 0.0;
        double along = 0.0;
        double lateral = distance;
        if (position->bearing_valid) {
            bearing_diff = angle_difference(record->bearing_deg, position->bearing_deg);
            if (bearing_diff > MAX_BEARING_DIFF_DEG) continue;
            along = dx * heading_east + dy * heading_north;
            lateral = fabs(dx * heading_north - dy * heading_east);
        }

        const double score = distance + bearing_diff * 12.0 + lateral * 0.10;
        consider(&nearest, record, distance, along, lateral, bearing_diff, score);
        if (position->bearing_valid && along <= 50.0) {
            consider(&passed, record, distance, along, lateral, bearing_diff, score);
        }
    }

    // Prefer the closest plausible sign already passed. A point-only database
    // cannot distinguish parallel roads, so reject geometrically weak matches
    // instead of presenting a confident but unrelated speed limit.
    const candidate_t *current = passed.valid ? &passed : (nearest.valid ? &nearest : NULL);
    if (!current) return false;
    result->current_speed_kmh = current->record->speed_kmh;
    result->current_distance_m = (uint32_t)llround(current->distance_m);
    result->current_lateral_m = (uint32_t)llround(current->lateral_m);
    result->current_bearing_diff_deg =
        (uint16_t)llround(current->bearing_diff_deg);
    if (position->bearing_valid) {
        if (current->distance_m > MAX_CURRENT_DISTANCE_M ||
            current->lateral_m > MAX_CURRENT_LATERAL_M ||
            current->bearing_diff_deg > MAX_CURRENT_BEARING_DIFF_DEG) {
            return false;
        }
    } else if (current->distance_m > MAX_NO_BEARING_DISTANCE_M) {
        return false;
    }

    result->current_valid = true;
    if (current->distance_m <= 150.0 && current->lateral_m <= 75.0 &&
        current->bearing_diff_deg <= 20.0) {
        result->confidence = 3;
    } else if (current->distance_m <= 500.0 && current->lateral_m <= 150.0 &&
               current->bearing_diff_deg <= 40.0) {
        result->confidence = 2;
    } else {
        result->confidence = 1;
    }

    if (position->bearing_valid) {
        candidate_t next = {0};
        for (uint32_t i = begin; i < end; ++i) {
            const db_record_t *record = &s_records[i];
            if (record->speed_kmh == result->current_speed_kmh) continue;
            const double bearing_diff = angle_difference(record->bearing_deg, position->bearing_deg);
            if (bearing_diff > MAX_NEXT_BEARING_DIFF_DEG) continue;
            const double record_lat = (double)record->latitude_e6 / 1000000.0;
            const double record_lon = (double)record->longitude_e6 / 1000000.0;
            const double dx = (record_lon - position->longitude) * METERS_PER_DEGREE * cos_latitude;
            const double dy = (record_lat - position->latitude) * METERS_PER_DEGREE;
            const double distance = sqrt(dx * dx + dy * dy);
            if (distance > SEARCH_RADIUS_M) continue;
            const double along = dx * heading_east + dy * heading_north;
            if (along <= 50.0) continue;
            const double lateral = fabs(dx * heading_north - dy * heading_east);
            if (lateral > MAX_NEXT_LATERAL_M) continue;
            const double score = distance + bearing_diff * 12.0 + lateral * 0.25;
            consider(&next, record, distance, along, lateral, bearing_diff, score);
        }
        if (next.valid) {
            result->next_valid = true;
            result->next_speed_kmh = next.record->speed_kmh;
            result->next_distance_m = (uint32_t)llround(next.distance_m);
        }
    }

    return true;
}
