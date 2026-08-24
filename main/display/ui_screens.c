// Car HUD UI - Layout mới:
//
// ┌──────────────────────────────────┐
// │ BLE    36min-12km   ┌────┐  85% │  (status bar + biển báo nhỏ góc phải)
// │                     │ 60 │      │
// │                     └────┘      │
// ├──────────────────────────────────┤
// │  ←                              │
// │       1.7 km                    │  (direction + distance LỚN)
// │                                 │
// │  CT37 Đ. Vành Đai 3            │  (tên đường)
// ├──────────────────────────────────┤
// │         69 km/h                 │  (tốc độ hiện tại)
// │     ETA: 10:20 AM              │
// └──────────────────────────────────┘

#include "ui_screens.h"

#include <stdio.h>
#include <string.h>

#include "esp_lvgl_port.h"

LV_FONT_DECLARE(lv_font_vi_14);
LV_FONT_DECLARE(lv_font_vi_20);
LV_FONT_DECLARE(lv_font_speed_64);

typedef struct {
    lv_obj_t *screen;

    // Status bar (top left)
    lv_obj_t *ble_label;
    lv_obj_t *battery_label;
    lv_obj_t *time_remaining_label;  // "36min - 12km"

    // Speed limit sign (TOP RIGHT, nhỏ ~60x60)
    lv_obj_t *limit_sign;
    lv_obj_t *limit_number;

    // Navigation (CENTER - phần chính)
    lv_obj_t *nav_direction_label;   // Ký hiệu hướng rẽ LỚN (font 64)
    lv_obj_t *nav_distance_label;    // Khoảng cách lớn (font 20)
    lv_obj_t *nav_road_label;        // Tên đường (font 14)

    // Current speed + ETA (BOTTOM)
    lv_obj_t *speed_circle;          // Hình tròn chứa tốc độ hiện tại
    lv_obj_t *speed_label;           // "69" số tốc độ bên trong circle
    lv_obj_t *eta_label;             // "ETA: 10:20 AM"

    // Volume overlay
    lv_obj_t *volume_overlay;
} ui_widgets_t;

static ui_widgets_t s_ui;

static lv_timer_t *s_bg_flash_timer = NULL;
static lv_timer_t *s_limit_blink_timer = NULL;

static int s_bg_flash_step = 0;
#define BG_FLASH_PERIOD_MS 250
#define BG_FLASH_STEPS 8

static void bg_flash_cb(lv_timer_t *timer)
{
    s_bg_flash_step++;
    bool red = (s_bg_flash_step % 2) == 1;
    lv_obj_set_style_bg_color(s_ui.screen, red ? lv_color_hex(0x3A0000) : lv_color_black(), 0);
    if (s_bg_flash_step >= BG_FLASH_STEPS) {
        lv_obj_set_style_bg_color(s_ui.screen, lv_color_black(), 0);
        lv_timer_del(timer);
        s_bg_flash_timer = NULL;
    }
}

static int s_limit_blink_step = 0;
#define LIMIT_BLINK_PERIOD_MS 350
#define LIMIT_BLINK_STEPS 8

static void limit_blink_cb(lv_timer_t *timer)
{
    s_limit_blink_step++;
    bool alt = (s_limit_blink_step % 2) == 1;
    lv_obj_set_style_border_color(s_ui.limit_sign,
        alt ? lv_color_hex(0xFFDD00) : lv_color_hex(0xE02020), 0);
    if (s_limit_blink_step >= LIMIT_BLINK_STEPS) {
        lv_obj_set_style_border_color(s_ui.limit_sign, lv_color_hex(0xE02020), 0);
        lv_timer_del(timer);
        s_limit_blink_timer = NULL;
    }
}

static void volume_hide_cb(lv_timer_t *timer)
{
    lv_obj_add_flag(s_ui.volume_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_del(timer);
}

void ui_init(lv_display_t *disp)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    s_ui.screen = scr;

    // === STATUS BAR (top, y=2) ===
    s_ui.ble_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.ble_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(s_ui.ble_label, &lv_font_vi_14, 0);
    lv_obj_align(s_ui.ble_label, LV_ALIGN_TOP_LEFT, 4, 2);
    lv_label_set_text(s_ui.ble_label, "...");

    s_ui.battery_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.battery_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(s_ui.battery_label, &lv_font_vi_14, 0);
    lv_obj_align(s_ui.battery_label, LV_ALIGN_TOP_RIGHT, -4, 2);
    lv_label_set_text(s_ui.battery_label, "");

    // Thời gian còn lại + khoảng cách tổng (giữa status bar)
    s_ui.time_remaining_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.time_remaining_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_ui.time_remaining_label, &lv_font_vi_14, 0);
    lv_obj_align(s_ui.time_remaining_label, LV_ALIGN_TOP_MID, -20, 2);
    lv_label_set_text(s_ui.time_remaining_label, "");

    // === SPEED LIMIT SIGN (top right, 72x72) ===
    s_ui.limit_sign = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.limit_sign);
    lv_obj_set_size(s_ui.limit_sign, 72, 72);
    lv_obj_set_style_radius(s_ui.limit_sign, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.limit_sign, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_ui.limit_sign, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ui.limit_sign, lv_color_hex(0xE02020), 0);
    lv_obj_set_style_border_width(s_ui.limit_sign, 6, 0);
    lv_obj_align(s_ui.limit_sign, LV_ALIGN_TOP_RIGHT, -4, 20);

    s_ui.limit_number = lv_label_create(s_ui.limit_sign);
    lv_obj_set_style_text_color(s_ui.limit_number, lv_color_black(), 0);
    // Dùng font_speed_64 nhưng scale nhỏ lại không được trong LVGL.
    // Dùng font_vi_20 nhưng set style bold-like (outline/shadow trick)
    lv_obj_set_style_text_font(s_ui.limit_number, &lv_font_vi_20, 0);
    lv_obj_set_style_text_letter_space(s_ui.limit_number, 2, 0);
    lv_obj_center(s_ui.limit_number);
    lv_label_set_text(s_ui.limit_number, "");

    // === NAVIGATION DIRECTION (DƯỚI speed circles, y=100+) ===
    // Dùng LV_FONT_DEFAULT (có FontAwesome symbols: LEFT, RIGHT, UP, DOWN)
    s_ui.nav_direction_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.nav_direction_label, lv_color_hex(0x33CC66), 0);
    lv_obj_set_style_text_font(s_ui.nav_direction_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(s_ui.nav_direction_label, LV_ALIGN_LEFT_MID, 10, 15);
    lv_label_set_text(s_ui.nav_direction_label, "");

    // Khoảng cách tới lượt rẽ (font 20, bên phải direction)
    s_ui.nav_distance_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.nav_distance_label, lv_color_hex(0x33FF66), 0);
    lv_obj_set_style_text_font(s_ui.nav_distance_label, &lv_font_vi_20, 0);
    lv_obj_align(s_ui.nav_distance_label, LV_ALIGN_LEFT_MID, 50, 15);
    lv_label_set_text(s_ui.nav_distance_label, "");

    // Tên đường sẽ rẽ vào (font 14, dưới direction)
    s_ui.nav_road_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.nav_road_label, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_text_font(s_ui.nav_road_label, &lv_font_vi_14, 0);
    lv_obj_set_style_text_align(s_ui.nav_road_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_ui.nav_road_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_ui.nav_road_label, 230);
    lv_obj_align(s_ui.nav_road_label, LV_ALIGN_LEFT_MID, 5, 50);
    lv_label_set_text(s_ui.nav_road_label, "");

    // === SPEED CIRCLE (bên trái biển báo giới hạn, 50x50) ===
    s_ui.speed_circle = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.speed_circle);
    lv_obj_set_size(s_ui.speed_circle, 50, 50);
    lv_obj_set_style_radius(s_ui.speed_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.speed_circle, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(s_ui.speed_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ui.speed_circle, lv_color_hex(0x33BBFF), 0);
    lv_obj_set_style_border_width(s_ui.speed_circle, 3, 0);
    // Vị trí: bên trái biển báo giới hạn (top right - 80px sang trái)
    lv_obj_align(s_ui.speed_circle, LV_ALIGN_TOP_RIGHT, -80, 32);

    s_ui.speed_label = lv_label_create(s_ui.speed_circle);
    lv_obj_set_style_text_color(s_ui.speed_label, lv_color_hex(0x33BBFF), 0);
    lv_obj_set_style_text_font(s_ui.speed_label, &lv_font_vi_20, 0);
    lv_obj_set_style_text_letter_space(s_ui.speed_label, 1, 0);
    lv_obj_center(s_ui.speed_label);
    lv_label_set_text(s_ui.speed_label, "0");

    s_ui.eta_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.eta_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_ui.eta_label, &lv_font_vi_14, 0);
    lv_obj_set_style_text_align(s_ui.eta_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_ui.eta_label, 230);
    lv_obj_align(s_ui.eta_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_label_set_text(s_ui.eta_label, "");

    // === VOLUME OVERLAY ===
    s_ui.volume_overlay = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.volume_overlay, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ui.volume_overlay, &lv_font_vi_20, 0);
    lv_obj_align(s_ui.volume_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_ui.volume_overlay, LV_OBJ_FLAG_HIDDEN);

    lvgl_port_unlock();
}

void ui_show_car_mode(void)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_ui.limit_number, "");
    lv_label_set_text(s_ui.speed_label, "0");
    lv_label_set_text(s_ui.nav_direction_label, "");
    lv_label_set_text(s_ui.nav_distance_label, "");
    lv_label_set_text(s_ui.nav_road_label, "");
    lv_label_set_text(s_ui.time_remaining_label, "");
    lv_label_set_text(s_ui.eta_label, "");
    lv_obj_add_flag(s_ui.limit_sign, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    ui_set_ble_connected(false);
}

void ui_car_update(uint16_t speed_kmh, uint16_t limit_kmh)
{
    lvgl_port_lock(0);

    // Biển báo giới hạn (góc trên phải)
    char buf[16];
    if (limit_kmh > 0) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)limit_kmh);
        lv_label_set_text(s_ui.limit_number, buf);
        lv_obj_clear_flag(s_ui.limit_sign, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.limit_sign, LV_OBJ_FLAG_HIDDEN);
    }

    // Tốc độ hiện tại (trong circle)
    snprintf(buf, sizeof(buf), "%u", (unsigned)speed_kmh);
    lv_label_set_text(s_ui.speed_label, buf);

    // Đổi màu viền circle + số nếu vượt tốc độ
    if (limit_kmh > 0 && speed_kmh > limit_kmh) {
        lv_obj_set_style_border_color(s_ui.speed_circle, lv_color_hex(0xFF2222), 0);
        lv_obj_set_style_text_color(s_ui.speed_label, lv_color_hex(0xFF2222), 0);
    } else {
        lv_obj_set_style_border_color(s_ui.speed_circle, lv_color_hex(0x33BBFF), 0);
        lv_obj_set_style_text_color(s_ui.speed_label, lv_color_hex(0x33BBFF), 0);
    }

    lvgl_port_unlock();
}

// Chuyển direction string thành LVGL symbol icons.
// LV_FONT_DEFAULT (FontAwesome) có: LEFT, RIGHT, UP, DOWN, REFRESH (rotate)
static const char *direction_to_arrow(const char *dir)
{
    if (!dir || !dir[0]) return "";
    if (strcmp(dir, "turn_left") == 0) return LV_SYMBOL_LEFT;
    if (strcmp(dir, "turn_right") == 0) return LV_SYMBOL_RIGHT;
    if (strcmp(dir, "straight") == 0) return LV_SYMBOL_UP;
    if (strcmp(dir, "slight_left") == 0) return LV_SYMBOL_LEFT;
    if (strcmp(dir, "slight_right") == 0) return LV_SYMBOL_RIGHT;
    if (strcmp(dir, "sharp_left") == 0) return LV_SYMBOL_LEFT LV_SYMBOL_LEFT;
    if (strcmp(dir, "sharp_right") == 0) return LV_SYMBOL_RIGHT LV_SYMBOL_RIGHT;
    if (strcmp(dir, "u_turn") == 0) return LV_SYMBOL_LOOP;
    if (strcmp(dir, "arrive") == 0) return LV_SYMBOL_OK;
    if (strcmp(dir, "roundabout") == 0) return LV_SYMBOL_REFRESH;
    if (strcmp(dir, "merge") == 0) return LV_SYMBOL_UP;
    if (strcmp(dir, "exit_right") == 0) return LV_SYMBOL_RIGHT;
    if (strcmp(dir, "exit_left") == 0) return LV_SYMBOL_LEFT;
    return LV_SYMBOL_UP;
}

void ui_nav_update(const char *direction, const char *distance, const char *road, const char *instruction)
{
    lvgl_port_lock(0);

    // Direction arrow
    if (direction && direction[0]) {
        const char *arrow = direction_to_arrow(direction);
        lv_label_set_text(s_ui.nav_direction_label, arrow);
    }

    // Khoảng cách
    if (distance && distance[0]) {
        lv_label_set_text(s_ui.nav_distance_label, distance);
    }

    // Tên đường
    if (road && road[0]) {
        lv_label_set_text(s_ui.nav_road_label, road);
    }

    // Parse time/eta từ instruction nếu có trong nav_data_t
    // (sẽ được xử lý riêng qua "time" và "eta" fields)

    lvgl_port_unlock();
}

void ui_nav_clear(void)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_ui.nav_direction_label, "");
    lv_label_set_text(s_ui.nav_distance_label, "");
    lv_label_set_text(s_ui.nav_road_label, "");
    lv_label_set_text(s_ui.time_remaining_label, "");
    lv_label_set_text(s_ui.eta_label, "");
    lvgl_port_unlock();
}

// Cap nhat time remaining, total distance, ETA
void ui_nav_update_meta(const char *time_remaining, const char *total_dist, const char *eta)
{
    lvgl_port_lock(0);

    // Time remaining + total dist hiện ở status bar giữa
    char meta[48] = "";
    if (time_remaining && time_remaining[0]) {
        snprintf(meta, sizeof(meta), "%s", time_remaining);
        if (total_dist && total_dist[0]) {
            size_t len = strlen(meta);
            snprintf(meta + len, sizeof(meta) - len, " - %s", total_dist);
        }
    } else if (total_dist && total_dist[0]) {
        snprintf(meta, sizeof(meta), "%s", total_dist);
    }
    lv_label_set_text(s_ui.time_remaining_label, meta);

    // ETA ở dưới cùng
    if (eta && eta[0]) {
        char eta_buf[32];
        snprintf(eta_buf, sizeof(eta_buf), "ETA: %s", eta);
        lv_label_set_text(s_ui.eta_label, eta_buf);
    }

    lvgl_port_unlock();
}

void ui_flash_over_limit(void)
{
    lvgl_port_lock(0);
    if (s_bg_flash_timer) lv_timer_del(s_bg_flash_timer);
    s_bg_flash_step = 0;
    s_bg_flash_timer = lv_timer_create(bg_flash_cb, BG_FLASH_PERIOD_MS, NULL);
    lvgl_port_unlock();
}

void ui_flash_limit_changed(void)
{
    lvgl_port_lock(0);
    if (s_limit_blink_timer) lv_timer_del(s_limit_blink_timer);
    s_limit_blink_step = 0;
    s_limit_blink_timer = lv_timer_create(limit_blink_cb, LIMIT_BLINK_PERIOD_MS, NULL);
    lvgl_port_unlock();
}

void ui_set_ble_connected(bool connected)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_ui.ble_label, connected ? "BLE" : "...");
    lv_obj_set_style_text_color(s_ui.ble_label,
        connected ? lv_color_hex(0x33CC66) : lv_color_hex(0x666666), 0);
    lvgl_port_unlock();
}

void ui_update_battery(uint8_t percent, bool charging)
{
    lvgl_port_lock(0);
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%d%%", charging ? "+" : "", percent);
    lv_label_set_text(s_ui.battery_label, buf);
    lvgl_port_unlock();
}

void ui_show_volume_overlay(uint8_t percent)
{
    lvgl_port_lock(0);
    char buf[16];
    snprintf(buf, sizeof(buf), "Vol %d%%", percent);
    lv_label_set_text(s_ui.volume_overlay, buf);
    lv_obj_clear_flag(s_ui.volume_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_t *t = lv_timer_create(volume_hide_cb, 1200, NULL);
    lv_timer_set_repeat_count(t, 1);
    lvgl_port_unlock();
}

void ui_vehicle_update(const vehicle_data_t *data)
{
    if (!data) return;
    if (data->speed_kmh >= 0) {
        lvgl_port_lock(0);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", data->speed_kmh);
        lv_label_set_text(s_ui.speed_label, buf);
        lvgl_port_unlock();
    }
}
