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
#include "nav_icons.h"

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
    lv_obj_t *weather_temp_bold_main; // bản sao lệch 1px của limit_number khi hiện nhiệt độ - giả đậm (không có font bold)
    lv_obj_t *weather_icon_main;     // badge thời tiết HÔM NAY (khi không có limit)

    // Navigation (CENTER - phần chính)
    lv_obj_t *nav_direction_img;     // Image icon hướng rẽ (32x32 bitmap)
    lv_obj_t *nav_distance_label;    // Khoảng cách tới lượt rẽ (font 20)
    lv_obj_t *nav_road_label;        // Google Maps: tên đường sẽ rẽ vào (font 14, vàng)
    lv_obj_t *location_label;        // Vietmap: vị trí hiện tại (font 14, trắng)

    // Current speed + ETA (BOTTOM)
    lv_obj_t *speed_circle;          // Hình tròn chứa tốc độ hiện tại
    lv_obj_t *speed_label;           // "69" số tốc độ bên trong circle
    lv_obj_t *eta_label;             // "ETA: 10:20 AM"

    // 2 vòng tròn nhỏ (dưới 2 vòng chính): biển báo sắp tới + camera
    lv_obj_t *next_limit_circle;
    lv_obj_t *next_limit_number;
    lv_obj_t *weather_temp_bold_next; // bản sao lệch 1px của next_limit_number khi hiện nhiệt độ
    lv_obj_t *next_limit_distance_label;  // khoảng cách, chữ nhỏ dưới vòng tròn
    lv_obj_t *weather_icon_next;     // badge thời tiết NGÀY MAI (khi không có next limit)
    lv_obj_t *camera_circle;         // nền vàng
    lv_obj_t *camera_number;
    lv_obj_t *camera_distance_label;      // khoảng cách, chữ nhỏ dưới vòng tròn

    // Volume overlay
    lv_obj_t *volume_overlay;
} ui_widgets_t;

static ui_widgets_t s_ui;

// Cache giá trị limit gần nhất — để ui_set_weather() (gọi SAU ui_car_update/
// ui_set_next_alert trong cùng 1 chu kỳ VMSX, xem app_main.c) biết có nên
// hiện thời tiết THAY số hay không, mà không cần đổi chữ ký các hàm đó.
static uint16_t s_last_limit_kmh;
static int16_t s_last_next_limit_kmh;

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

// Dat text cho label trong 2 vong tron LON (limit_sign/speed_circle): dung
// font_speed_64 (64px) cho so thuc, nhung font nay CHI co glyph cho so/':'
// '/' 'h' 'k' 'm' (subset de tiet kiem flash) - khong co '!' hay '-', hien
// thi thanh hinh chu nhat loi (tofu box) neu dung. Placeholder ("!") phai
// chuyen sang lv_font_vi_20 (nho hon nhung co du glyph) de hien dung.
static void set_big_circle_text(lv_obj_t *label, const char *text, bool is_number)
{
    lv_obj_set_style_text_font(label, is_number ? &lv_font_speed_64 : &lv_font_vi_20, 0);
    lv_label_set_text(label, text);
}

// Không có font bold riêng (chỉ có vi_14/vi_20/speed_64, không có biến thể
// đậm — không có công cụ tạo font trong môi trường này để build thêm) nên
// "to hơn + đậm hơn" cho chữ nhiệt độ (thời tiết) được giả lập bằng 2 kỹ
// thuật kết hợp:
//  1. To hơn: transform scale (phóng to xung quanh tâm chữ) trên chính label.
//  2. Đậm hơn: vẽ chồng 1 label thứ 2 giống hệt, lệch 1px, cùng scale —
//     "poor man's bold" quen thuộc khi không có font đậm.
#define WEATHER_TEXT_SCALE 384 // 150% (LV_SCALE_NONE = 256 = 100%)

static void apply_weather_text_scale(lv_obj_t *label)
{
    lv_obj_set_style_transform_pivot_x(label, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(label, lv_pct(50), 0);
    lv_obj_set_style_transform_scale_x(label, WEATHER_TEXT_SCALE, 0);
    lv_obj_set_style_transform_scale_y(label, WEATHER_TEXT_SCALE, 0);
}

static void reset_weather_text_scale(lv_obj_t *label)
{
    lv_obj_set_style_transform_scale_x(label, LV_SCALE_NONE, 0);
    lv_obj_set_style_transform_scale_y(label, LV_SCALE_NONE, 0);
}

void ui_init(lv_display_t *disp)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    // Man hinh scrollable mac dinh cua LVGL - vai widget (label/canvas) nam
    // sat/vuot mep duoi (y~222-238 tren canvas 240) khien LVGL coi noi dung
    // la "co the scroll" va tu ve 1 vach scrollbar doc ben phai man hinh.
    // Board khong can scroll gi ca - tat han.
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
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

    // === SPEED LIMIT SIGN (to nhat, 128x128) - day cao gan status bar ===
    s_ui.limit_sign = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.limit_sign);
    lv_obj_set_size(s_ui.limit_sign, 128, 128);
    lv_obj_set_style_radius(s_ui.limit_sign, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.limit_sign, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_ui.limit_sign, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ui.limit_sign, lv_color_hex(0xE02020), 0);
    lv_obj_set_style_border_width(s_ui.limit_sign, 7, 0);
    lv_obj_align(s_ui.limit_sign, LV_ALIGN_TOP_LEFT, 4, 22);

    s_ui.limit_number = lv_label_create(s_ui.limit_sign);
    lv_obj_set_style_text_color(s_ui.limit_number, lv_color_black(), 0);
    lv_obj_set_style_text_letter_space(s_ui.limit_number, 0, 0);
    lv_obj_center(s_ui.limit_number);
    set_big_circle_text(s_ui.limit_number, "!", false);

    // Bản sao "giả đậm" của limit_number, chỉ dùng khi hiện nhiệt độ (xem
    // ui_set_weather()) — lệch 1px, ẩn mặc định.
    s_ui.weather_temp_bold_main = lv_label_create(s_ui.limit_sign);
    lv_obj_set_style_text_color(s_ui.weather_temp_bold_main, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_ui.weather_temp_bold_main, &lv_font_vi_20, 0);
    lv_obj_align(s_ui.weather_temp_bold_main, LV_ALIGN_CENTER, 1, 0);
    lv_obj_add_flag(s_ui.weather_temp_bold_main, LV_OBJ_FLAG_HIDDEN);

    // Badge thời tiết HÔM NAY - vùng trên của limit_sign, phía trên số/chữ
    // (luôn ở giữa). Ẩn mặc định, chỉ hiện khi không có limit (xem
    // ui_set_weather()).
    s_ui.weather_icon_main = lv_obj_create(s_ui.limit_sign);
    lv_obj_remove_style_all(s_ui.weather_icon_main);
    lv_obj_set_size(s_ui.weather_icon_main, 28, 28);
    lv_obj_set_style_radius(s_ui.weather_icon_main, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_ui.weather_icon_main, LV_OPA_COVER, 0);
    lv_obj_align(s_ui.weather_icon_main, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_add_flag(s_ui.weather_icon_main, LV_OBJ_FLAG_HIDDEN);

    // === NAVIGATION DIRECTION (DƯỚI speed circles, y=100+) ===
    // Dùng lv_image 32x32 cho icon hướng rẽ rõ ràng
    s_ui.nav_direction_img = lv_image_create(scr);
    lv_obj_set_size(s_ui.nav_direction_img, 24, 24);
    lv_obj_align(s_ui.nav_direction_img, LV_ALIGN_TOP_LEFT, 8, 156);
    lv_image_set_src(s_ui.nav_direction_img, &nav_icon_straight);
    lv_obj_set_style_image_recolor(s_ui.nav_direction_img, lv_color_hex(0x33CC66), 0);
    lv_obj_set_style_image_recolor_opa(s_ui.nav_direction_img, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_ui.nav_direction_img, LV_OBJ_FLAG_HIDDEN);

    // Khoảng cách tới lượt rẽ hoặc instruction (font 20, bên phải direction)
    s_ui.nav_distance_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.nav_distance_label, lv_color_hex(0x33FF66), 0);
    lv_obj_set_style_text_font(s_ui.nav_distance_label, &lv_font_vi_20, 0);
    lv_label_set_long_mode(s_ui.nav_distance_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_ui.nav_distance_label, 194);
    lv_obj_align(s_ui.nav_distance_label, LV_ALIGN_TOP_LEFT, 38, 158);
    lv_label_set_text(s_ui.nav_distance_label, "");

    // Tên đường sẽ rẽ vào - Google Maps (font 14, vàng)
    s_ui.nav_road_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.nav_road_label, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_text_font(s_ui.nav_road_label, &lv_font_vi_14, 0);
    lv_obj_set_style_text_align(s_ui.nav_road_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.nav_road_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_ui.nav_road_label, 232);
    lv_obj_align(s_ui.nav_road_label, LV_ALIGN_TOP_LEFT, 4, 176);
    lv_label_set_text(s_ui.nav_road_label, "");

    // Vị trí hiện tại - Vietmap Live (font 14, trắng, dòng dưới)
    s_ui.location_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.location_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(s_ui.location_label, &lv_font_vi_14, 0);
    lv_obj_set_style_text_align(s_ui.location_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.location_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_ui.location_label, 232);
    lv_obj_align(s_ui.location_label, LV_ALIGN_TOP_LEFT, 4, 190);
    lv_label_set_text(s_ui.location_label, "");

    // === SPEED CIRCLE (nho hon limit_sign, 88x88), can giua theo chieu doc
    // voi limit_sign (top = 22 + (128-88)/2 = 42) ===
    s_ui.speed_circle = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.speed_circle);
    lv_obj_set_size(s_ui.speed_circle, 88, 88);
    lv_obj_set_style_radius(s_ui.speed_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.speed_circle, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(s_ui.speed_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ui.speed_circle, lv_color_hex(0x33BBFF), 0);
    lv_obj_set_style_border_width(s_ui.speed_circle, 4, 0);
    lv_obj_align(s_ui.speed_circle, LV_ALIGN_TOP_RIGHT, -8, 42);

    s_ui.speed_label = lv_label_create(s_ui.speed_circle);
    lv_obj_set_style_text_color(s_ui.speed_label, lv_color_hex(0x33BBFF), 0);
    lv_obj_set_style_text_font(s_ui.speed_label, &lv_font_speed_64, 0);
    lv_obj_set_style_text_letter_space(s_ui.speed_label, 0, 0);
    lv_obj_center(s_ui.speed_label);
    lv_label_set_text(s_ui.speed_label, "0");

    // === 2 VONG TRON NHO (duoi limit_sign/speed_circle, 60x60): bien bao
    // toc do sap toi (trai, trang/vien do giong limit_sign) + camera/canh
    // bao (phai, NEN VANG) ===
    s_ui.next_limit_circle = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.next_limit_circle);
    lv_obj_set_size(s_ui.next_limit_circle, 60, 60);
    lv_obj_set_style_radius(s_ui.next_limit_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.next_limit_circle, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_ui.next_limit_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ui.next_limit_circle, lv_color_hex(0xE02020), 0);
    lv_obj_set_style_border_width(s_ui.next_limit_circle, 4, 0);
    lv_obj_align(s_ui.next_limit_circle, LV_ALIGN_TOP_LEFT, 30, 160);

    s_ui.next_limit_number = lv_label_create(s_ui.next_limit_circle);
    lv_obj_set_style_text_color(s_ui.next_limit_number, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_ui.next_limit_number, &lv_font_vi_20, 0);
    lv_obj_center(s_ui.next_limit_number);
    lv_label_set_text(s_ui.next_limit_number, "!");

    // Bản sao "giả đậm" của next_limit_number, chỉ dùng khi hiện nhiệt độ
    // (xem ui_set_weather()) — lệch 1px, ẩn mặc định.
    s_ui.weather_temp_bold_next = lv_label_create(s_ui.next_limit_circle);
    lv_obj_set_style_text_color(s_ui.weather_temp_bold_next, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_ui.weather_temp_bold_next, &lv_font_vi_20, 0);
    lv_obj_align(s_ui.weather_temp_bold_next, LV_ALIGN_CENTER, 1, 0);
    lv_obj_add_flag(s_ui.weather_temp_bold_next, LV_OBJ_FLAG_HIDDEN);

    // Badge thời tiết NGÀY MAI - vùng trên của next_limit_circle. Ẩn mặc
    // định, chỉ hiện khi không có next limit (xem ui_set_weather()). Lưu ý:
    // icon cảnh báo thật (img_stream.c, khi có) đè lên TRÊN badge này khi
    // hiện - hợp lý vì icon thật ưu tiên hơn thời tiết.
    s_ui.weather_icon_next = lv_obj_create(s_ui.next_limit_circle);
    lv_obj_remove_style_all(s_ui.weather_icon_next);
    lv_obj_set_size(s_ui.weather_icon_next, 14, 14);
    lv_obj_set_style_radius(s_ui.weather_icon_next, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_ui.weather_icon_next, LV_OPA_COVER, 0);
    lv_obj_align(s_ui.weather_icon_next, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_add_flag(s_ui.weather_icon_next, LV_OBJ_FLAG_HIDDEN);

    // Khoảng cách tới biển báo sắp tới đó - chữ nhỏ ngay dưới vòng tròn.
    s_ui.next_limit_distance_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.next_limit_distance_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(s_ui.next_limit_distance_label, &lv_font_vi_14, 0);
    lv_obj_set_style_text_align(s_ui.next_limit_distance_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_ui.next_limit_distance_label, 60);
    lv_obj_align(s_ui.next_limit_distance_label, LV_ALIGN_TOP_LEFT, 30, 222);
    lv_label_set_text(s_ui.next_limit_distance_label, "--");

    s_ui.camera_circle = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.camera_circle);
    lv_obj_set_size(s_ui.camera_circle, 60, 60);
    lv_obj_set_style_radius(s_ui.camera_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.camera_circle, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_bg_opa(s_ui.camera_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ui.camera_circle, lv_color_hex(0x9C7A00), 0);
    lv_obj_set_style_border_width(s_ui.camera_circle, 4, 0);
    lv_obj_align(s_ui.camera_circle, LV_ALIGN_TOP_RIGHT, -30, 160);

    s_ui.camera_number = lv_label_create(s_ui.camera_circle);
    lv_obj_set_style_text_color(s_ui.camera_number, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_ui.camera_number, &lv_font_vi_20, 0);
    lv_obj_center(s_ui.camera_number);
    lv_label_set_text(s_ui.camera_number, "--");

    // Khoảng cách tới camera - chữ nhỏ ngay dưới vòng tròn.
    s_ui.camera_distance_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.camera_distance_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(s_ui.camera_distance_label, &lv_font_vi_14, 0);
    lv_obj_set_style_text_align(s_ui.camera_distance_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_ui.camera_distance_label, 60);
    lv_obj_align(s_ui.camera_distance_label, LV_ALIGN_TOP_RIGHT, -30, 222);
    lv_label_set_text(s_ui.camera_distance_label, "--");

    s_ui.eta_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.eta_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_ui.eta_label, &lv_font_vi_14, 0);
    lv_obj_set_style_text_align(s_ui.eta_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_ui.eta_label, 230);
    lv_obj_align(s_ui.eta_label, LV_ALIGN_BOTTOM_MID, 0, -4);
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
    // "!"/"--" khi chưa/không có giá trị hợp lệ, giống bong bóng VietMap
    // Live - các vòng tròn LUÔN hiện (không ẩn đi) để khớp UI bong bóng.
    set_big_circle_text(s_ui.limit_number, "!", false);
    lv_label_set_text(s_ui.speed_label, "0");
    lv_obj_add_flag(s_ui.nav_direction_img, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_ui.nav_distance_label, "");
    lv_label_set_text(s_ui.nav_road_label, "");
    lv_label_set_text(s_ui.location_label, "");
    lv_label_set_text(s_ui.time_remaining_label, "");
    lv_label_set_text(s_ui.eta_label, "");
    lv_obj_clear_flag(s_ui.limit_sign, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_ui.next_limit_number, "!");
    lv_label_set_text(s_ui.next_limit_distance_label, "--");
    lv_label_set_text(s_ui.camera_number, "--");
    lv_label_set_text(s_ui.camera_distance_label, "--");
    s_last_limit_kmh = 0;
    s_last_next_limit_kmh = 0;
    lvgl_port_unlock();
    ui_set_ble_connected(false);
}

void ui_car_update(uint16_t speed_kmh, uint16_t limit_kmh)
{
    lvgl_port_lock(0);

    // Biển báo giới hạn: LUÔN hiện (giống bong bóng VietMap Live) - "!" khi
    // limit_kmh=0 (chưa nhận được/không hợp lệ) thay vì ẩn hẳn circle đi.
    char buf[16];
    if (limit_kmh > 0) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)limit_kmh);
        set_big_circle_text(s_ui.limit_number, buf, true);
        reset_weather_text_scale(s_ui.limit_number);
        lv_obj_add_flag(s_ui.weather_icon_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.weather_temp_bold_main, LV_OBJ_FLAG_HIDDEN);
    } else {
        // "!" tạm thời — ui_set_weather() (gọi ngay sau, cùng chu kỳ VMSX)
        // sẽ thay bằng thời tiết hôm nay nếu có (áp lại scale khi đó).
        set_big_circle_text(s_ui.limit_number, "!", false);
        reset_weather_text_scale(s_ui.limit_number);
        lv_obj_add_flag(s_ui.weather_temp_bold_main, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_ui.limit_sign, LV_OBJ_FLAG_HIDDEN);
    s_last_limit_kmh = limit_kmh;

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

// Chuyển direction string thành navigation icon bitmap.
static const lv_image_dsc_t *direction_to_icon(const char *dir)
{
    if (!dir || !dir[0]) return &nav_icon_straight;
    if (strcmp(dir, "turn_left") == 0) return &nav_icon_turn_left;
    if (strcmp(dir, "turn_right") == 0) return &nav_icon_turn_right;
    if (strcmp(dir, "straight") == 0) return &nav_icon_straight;
    if (strcmp(dir, "slight_left") == 0) return &nav_icon_slight_left;
    if (strcmp(dir, "slight_right") == 0) return &nav_icon_slight_right;
    if (strcmp(dir, "sharp_left") == 0) return &nav_icon_turn_left;
    if (strcmp(dir, "sharp_right") == 0) return &nav_icon_turn_right;
    if (strcmp(dir, "u_turn") == 0) return &nav_icon_uturn;
    if (strcmp(dir, "arrive") == 0) return &nav_icon_arrive;
    if (strcmp(dir, "roundabout") == 0) return &nav_icon_uturn; // reuse
    if (strcmp(dir, "merge") == 0) return &nav_icon_straight;
    if (strcmp(dir, "exit_right") == 0) return &nav_icon_slight_right;
    if (strcmp(dir, "exit_left") == 0) return &nav_icon_slight_left;
    return &nav_icon_straight;
}

void ui_nav_update(const char *direction, const char *distance, const char *road, const char *instruction)
{
    lvgl_port_lock(0);

    // Direction icon (32x32 bitmap)
    if (direction && direction[0]) {
        const lv_image_dsc_t *icon = direction_to_icon(direction);
        lv_image_set_src(s_ui.nav_direction_img, icon);
        lv_obj_clear_flag(s_ui.nav_direction_img, LV_OBJ_FLAG_HIDDEN);
    }

    // Khoảng cách: ưu tiên distance, nếu không có thì hiện instruction
    if (distance && distance[0]) {
        lv_label_set_text(s_ui.nav_distance_label, distance);
    } else if (instruction && instruction[0]) {
        lv_label_set_text(s_ui.nav_distance_label, instruction);
    }

    // Tên đường sẽ rẽ vào
    if (road && road[0]) {
        lv_label_set_text(s_ui.nav_road_label, road);
    }

    lvgl_port_unlock();
}

void ui_nav_clear(void){
    lvgl_port_lock(0);
    lv_obj_add_flag(s_ui.nav_direction_img, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_ui.nav_distance_label, "");
    lv_label_set_text(s_ui.nav_road_label, "");
    lv_label_set_text(s_ui.location_label, "");
    lv_label_set_text(s_ui.time_remaining_label, "");
    lv_label_set_text(s_ui.eta_label, "");
    lvgl_port_unlock();
}

void ui_set_location(const char *location)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_ui.location_label, location ? location : "");
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
        char eta_buf[48];
        // Không thêm "ETA:" nếu text đã chứa "Dự kiến" hoặc "ETA"
        if (strstr(eta, "kiến") || strstr(eta, "ETA")) {
            snprintf(eta_buf, sizeof(eta_buf), "%s", eta);
        } else {
            snprintf(eta_buf, sizeof(eta_buf), "ETA: %s", eta);
        }
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

    // Hiển thị TPMS nếu có data (dùng dòng location_label khi không navigate,
    // hoặc dòng eta_label nếu đang navigate)
    bool has_tpms = data->tire_fl_kpa > 0 || data->tire_fr_kpa > 0 ||
                    data->tire_rl_kpa > 0 || data->tire_rr_kpa > 0;
    if (has_tpms) {
        lvgl_port_lock(0);
        // Format: "FL:2.1 FR:2.1 RL:2.2 RR:2.2" (bar, 1 decimal)
        char tpms_buf[64];
        snprintf(tpms_buf, sizeof(tpms_buf), "FL:%.1f FR:%.1f RL:%.1f RR:%.1f",
                 data->tire_fl_kpa / 100.0,
                 data->tire_fr_kpa / 100.0,
                 data->tire_rl_kpa / 100.0,
                 data->tire_rr_kpa / 100.0);

        // Hiển thị ở dòng ETA (dưới cùng)
        lv_label_set_text(s_ui.eta_label, tpms_buf);

        // Đổi màu nếu áp suất thấp (< 180 kPa = 1.8 bar) hoặc cao (> 280 kPa)
        bool warning = false;
        int16_t tires[] = {data->tire_fl_kpa, data->tire_fr_kpa,
                           data->tire_rl_kpa, data->tire_rr_kpa};
        for (int i = 0; i < 4; i++) {
            if (tires[i] > 0 && (tires[i] < 180 || tires[i] > 280)) {
                warning = true;
                break;
            }
        }
        lv_obj_set_style_text_color(s_ui.eta_label,
            warning ? lv_color_hex(0xFF6600) : lv_color_hex(0x888888), 0);

        lvgl_port_unlock();
    }
}


// "850m" / "1.2km" hoac "--" neu <= 0.
static void format_distance(char *buf, size_t buf_size, int32_t distance_m)
{
    if (distance_m <= 0) {
        snprintf(buf, buf_size, "--");
        return;
    }
    if (distance_m >= 1000) {
        snprintf(buf, buf_size, "%d.%dkm",
                 (int)(distance_m / 1000),
                 (int)((distance_m % 1000) / 100));
    } else {
        snprintf(buf, buf_size, "%dm", (int)distance_m);
    }
}

// next_limit_kmh/next_limit_distance_m va camera_distance_m la 2 canh bao
// DOC LAP (bong bong co 2 khu rieng, xac nhan qua dump that: vd trai 186m
// / phai 365m cung luc) - moi ben hien dung khoang cach cua chinh no.
void ui_set_next_alert(int16_t next_limit_kmh, int32_t next_limit_distance_m, int32_t camera_distance_m)
{
    lvgl_port_lock(0);

    char buf[16];
    if (next_limit_kmh > 0) {
        snprintf(buf, sizeof(buf), "%d", (int)next_limit_kmh);
        lv_label_set_text(s_ui.next_limit_number, buf);
        reset_weather_text_scale(s_ui.next_limit_number);
        lv_obj_add_flag(s_ui.weather_icon_next, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.weather_temp_bold_next, LV_OBJ_FLAG_HIDDEN);
    } else {
        // "!" tạm thời — ui_set_weather() (gọi ngay sau, cùng chu kỳ VMSX)
        // sẽ thay bằng thời tiết ngày mai nếu có (áp lại scale khi đó).
        lv_label_set_text(s_ui.next_limit_number, "!");
        reset_weather_text_scale(s_ui.next_limit_number);
        lv_obj_add_flag(s_ui.weather_temp_bold_next, LV_OBJ_FLAG_HIDDEN);
    }
    s_last_next_limit_kmh = next_limit_kmh;

    format_distance(buf, sizeof(buf), next_limit_distance_m);
    lv_label_set_text(s_ui.next_limit_distance_label, buf);

    format_distance(buf, sizeof(buf), camera_distance_m);
    lv_label_set_text(s_ui.camera_distance_label, buf);
    lv_label_set_text(s_ui.camera_number, buf);

    lvgl_port_unlock();
}

// Mau badge theo dieu kien thoi tiet (0=nang,1=may,2=mua,3=giong,4=tuyet/
// suong) - khong co bo icon anh rieng nen dung mau lam dau hieu truc quan
// don gian, nhe cho board.
static lv_color_t weather_condition_color(uint8_t condition)
{
    switch (condition) {
        case 0: return lv_color_hex(0xFFB800); // nang - vang cam
        case 2: return lv_color_hex(0x3388DD); // mua - xanh duong
        case 3: return lv_color_hex(0x6644AA); // giong - tim
        case 4: return lv_color_hex(0xCCEEFF); // tuyet/suong mu - xanh nhat
        case 1:
        default: return lv_color_hex(0xAAAAAA); // may - xam
    }
}

// Hien thi thay so trong 2 vong tron khi KHONG co du lieu tuong ung (xem
// s_last_limit_kmh/s_last_next_limit_kmh, cache boi ui_car_update()/
// ui_set_next_alert() ngay truoc do trong cung 1 chu ky VMSX). Icon anh
// canh bao that (img_stream.c), khi co, ve DE LEN TREN badge nay - uu tien
// hon thoi tiet.
void ui_set_weather(bool today_valid, int8_t today_temp_c, uint8_t today_condition,
                     bool tomorrow_valid, int8_t tomorrow_temp_c, uint8_t tomorrow_condition)
{
    lvgl_port_lock(0);

    char buf[16];
    if (s_last_limit_kmh == 0) {
        if (today_valid) {
            snprintf(buf, sizeof(buf), "%d°", (int)today_temp_c);
            set_big_circle_text(s_ui.limit_number, buf, false);
            apply_weather_text_scale(s_ui.limit_number);
            lv_label_set_text(s_ui.weather_temp_bold_main, buf);
            apply_weather_text_scale(s_ui.weather_temp_bold_main);
            lv_obj_clear_flag(s_ui.weather_temp_bold_main, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_ui.weather_icon_main, weather_condition_color(today_condition), 0);
            lv_obj_clear_flag(s_ui.weather_icon_main, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.weather_icon_main, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_ui.weather_temp_bold_main, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_last_next_limit_kmh <= 0) {
        if (tomorrow_valid) {
            snprintf(buf, sizeof(buf), "%d°", (int)tomorrow_temp_c);
            lv_label_set_text(s_ui.next_limit_number, buf);
            apply_weather_text_scale(s_ui.next_limit_number);
            lv_label_set_text(s_ui.weather_temp_bold_next, buf);
            apply_weather_text_scale(s_ui.weather_temp_bold_next);
            lv_obj_clear_flag(s_ui.weather_temp_bold_next, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_ui.weather_icon_next, weather_condition_color(tomorrow_condition), 0);
            lv_obj_clear_flag(s_ui.weather_icon_next, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.weather_icon_next, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_ui.weather_temp_bold_next, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lvgl_port_unlock();
}

lv_obj_t *ui_get_next_limit_circle(void)
{
    return s_ui.next_limit_circle;
}

lv_obj_t *ui_get_camera_circle(void)
{
    return s_ui.camera_circle;
}
