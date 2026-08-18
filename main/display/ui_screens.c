#include "ui_screens.h"

#include <stdio.h>
#include <string.h>

#include "esp_lvgl_port.h"

// Font tieng Viet tu generate (Arial Unicode -> LVGL bitmap font), xem
// main/display/fonts/. Dung font nay cho MOI label co the chua chu co dau;
// icon (LV_SYMBOL_*) van dung LV_FONT_DEFAULT vi la font rieng chi co bitmap
// cho vung ky tu FontAwesome, khong co bang chu Viet.
LV_FONT_DECLARE(lv_font_vi_14);
LV_FONT_DECLARE(lv_font_vi_20);
LV_FONT_DECLARE(lv_font_speed_64);

typedef struct {
    lv_obj_t *screen;             // man hinh chinh (dung de nhap nhay nen)
    lv_obj_t *status_bar_label;   // goc tren trai: trang thai wifi/AP
    lv_obj_t *battery_label;      // goc tren phai: % pin
    lv_obj_t *icon_label;         // icon LV_SYMBOL_*, mau doi theo state
    lv_obj_t *title_label;        // ten trang thai ngan, chu lon
    lv_obj_t *subtitle_label;     // huong dan / thong bao loi / thong tin provisioning
    lv_obj_t *text_container;     // khung chua 2 dong transcript, xep flex-column
    lv_obj_t *user_text_label;    // "Ban: ..." transcript nguoi dung (STT)
    lv_obj_t *ai_text_label;      // "AI: ..." transcript Gemini tra loi
    lv_obj_t *volume_overlay;     // icon+% volume, an/hien tam thoi

    // "Car Mode": hien thi toc do xe + bien bao gioi han
    lv_obj_t *car_speed_label;    // "XX km/h" nho, ben duoi bien bao
    lv_obj_t *car_limit_ring;     // vong tron xanh bao quanh, the hien % toc do/gioi han
    lv_obj_t *car_limit_sign;     // hinh tron vien do lon, kieu bien bao gioi han
    lv_obj_t *car_limit_label;    // so gioi han lon, ben trong bien bao
} ui_widgets_t;

static ui_widgets_t s_ui;

// Timer nhap nhay (khai bao truoc de set_texts co the huy khi chuyen man hinh).
static lv_timer_t *s_bg_flash_timer = NULL;
static lv_timer_t *s_limit_blink_timer = NULL;

// Bat/tat toan bo widget cua che do tro ly giong noi (khong dung cho Car Mode).
static void set_assistant_widgets_visible(bool visible)
{
    lv_obj_t *widgets[] = {
        s_ui.icon_label, s_ui.title_label, s_ui.subtitle_label, s_ui.text_container,
    };
    for (size_t i = 0; i < sizeof(widgets) / sizeof(widgets[0]); i++) {
        if (visible) {
            lv_obj_clear_flag(widgets[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widgets[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void set_icon(const char *symbol, lv_color_t color)
{
    lv_label_set_text(s_ui.icon_label, symbol);
    lv_obj_set_style_text_color(s_ui.icon_label, color, 0);
}

static void set_texts(const char *title, const char *subtitle)
{
    set_assistant_widgets_visible(true);
    lv_obj_add_flag(s_ui.car_speed_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.car_limit_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.car_limit_sign, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_ui.title_label, title);
    lv_label_set_text(s_ui.subtitle_label, subtitle);

    // Roi Car Mode giua chung luc dang nhap nhay - huy ngay, tra man hinh ve binh thuong.
    if (s_bg_flash_timer) {
        lv_timer_del(s_bg_flash_timer);
        s_bg_flash_timer = NULL;
        lv_obj_set_style_bg_color(s_ui.screen, lv_color_black(), 0);
    }
    if (s_limit_blink_timer) {
        lv_timer_del(s_limit_blink_timer);
        s_limit_blink_timer = NULL;
        lv_obj_set_style_border_color(s_ui.car_limit_sign, lv_color_hex(0xE02020), 0);
    }
}

static void volume_overlay_hide_cb(lv_timer_t *timer)
{
    lv_obj_add_flag(s_ui.volume_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_del(timer);
}

// ---- Nhap nhay nen do khi vuot toc do gioi han (~2s) ----
static int s_bg_flash_step = 0;
#define BG_FLASH_PERIOD_MS 250
#define BG_FLASH_STEPS     8 // 8 x 250ms = 2s

static void bg_flash_cb(lv_timer_t *timer)
{
    s_bg_flash_step++;
    bool red_on = (s_bg_flash_step % 2) == 1;
    lv_obj_set_style_bg_color(s_ui.screen, red_on ? lv_color_hex(0x3A0000) : lv_color_black(), 0);
    if (s_bg_flash_step >= BG_FLASH_STEPS) {
        lv_obj_set_style_bg_color(s_ui.screen, lv_color_black(), 0);
        lv_timer_del(timer);
        s_bg_flash_timer = NULL;
    }
}

// ---- Nhap nhay vien bien bao khi gioi han toc do thay doi (~3s) ----
static int s_limit_blink_step = 0;
#define LIMIT_BLINK_PERIOD_MS 350
#define LIMIT_BLINK_STEPS     8 // 8 x 350ms = 2.8s

static void limit_blink_cb(lv_timer_t *timer)
{
    s_limit_blink_step++;
    bool alt = (s_limit_blink_step % 2) == 1;
    lv_obj_set_style_border_color(s_ui.car_limit_sign, alt ? lv_color_hex(0xFFDD00) : lv_color_hex(0xE02020), 0);
    if (s_limit_blink_step >= LIMIT_BLINK_STEPS) {
        lv_obj_set_style_border_color(s_ui.car_limit_sign, lv_color_hex(0xE02020), 0);
        lv_timer_del(timer);
        s_limit_blink_timer = NULL;
    }
}

// Goi khi da giu lvgl_port_lock() roi (tranh lock long nhau tu cac ham
// ui_show_*). ui_clear_transcripts() (public) se tu lock rieng.
static void clear_transcripts_locked(void)
{
    lv_label_set_text(s_ui.user_text_label, "");
    lv_label_set_text(s_ui.ai_text_label, "");
}

void ui_init(lv_display_t *disp)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    s_ui.screen = scr;

    s_ui.status_bar_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.status_bar_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ui.status_bar_label, &lv_font_vi_14, 0);
    lv_obj_align(s_ui.status_bar_label, LV_ALIGN_TOP_LEFT, 6, 4);
    lv_label_set_text(s_ui.status_bar_label, "");

    s_ui.battery_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.battery_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ui.battery_label, &lv_font_vi_14, 0);
    lv_obj_align(s_ui.battery_label, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_label_set_text(s_ui.battery_label, "");

    s_ui.icon_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_ui.icon_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(s_ui.icon_label, LV_ALIGN_TOP_MID, 0, 22);
    set_icon(LV_SYMBOL_REFRESH, lv_color_hex(0xFFAA00));

    s_ui.title_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ui.title_label, &lv_font_vi_20, 0);
    lv_obj_align(s_ui.title_label, LV_ALIGN_TOP_MID, 0, 46);

    s_ui.subtitle_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.subtitle_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_ui.subtitle_label, &lv_font_vi_14, 0);
    lv_obj_set_style_text_align(s_ui.subtitle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_ui.subtitle_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.subtitle_label, 220);
    lv_obj_align(s_ui.subtitle_label, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_label_set_text(s_ui.subtitle_label, "");

    s_ui.text_container = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.text_container);
    lv_obj_set_size(s_ui.text_container, 226, 130);
    lv_obj_align(s_ui.text_container, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_flex_flow(s_ui.text_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ui.text_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_ui.text_container, 6, 0);
    lv_obj_set_style_pad_left(s_ui.text_container, 4, 0);

    s_ui.user_text_label = lv_label_create(s_ui.text_container);
    lv_obj_set_style_text_color(s_ui.user_text_label, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(s_ui.user_text_label, &lv_font_vi_14, 0);
    lv_label_set_long_mode(s_ui.user_text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.user_text_label, 218);
    lv_label_set_text(s_ui.user_text_label, "");

    s_ui.ai_text_label = lv_label_create(s_ui.text_container);
    lv_obj_set_style_text_color(s_ui.ai_text_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ui.ai_text_label, &lv_font_vi_14, 0);
    lv_label_set_long_mode(s_ui.ai_text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.ai_text_label, 218);
    lv_label_set_text(s_ui.ai_text_label, "");

    s_ui.volume_overlay = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.volume_overlay, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ui.volume_overlay, &lv_font_vi_14, 0);
    lv_obj_align(s_ui.volume_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_ui.volume_overlay, LV_OBJ_FLAG_HIDDEN);

    // ---- Car Mode: an mac dinh, chi hien khi ui_show_car_mode() duoc goi ----
    // Vong tron xanh bao quanh bien bao, the hien % toc do hien tai / gioi
    // han (vd toc do = 80% gioi han thi vong day 80%). Tao TRUOC bien bao de
    // bien bao (opaque, nho hon) de len tren, chi lo phan vanh ngoai cua vong.
    s_ui.car_limit_ring = lv_arc_create(scr);
    lv_obj_remove_style(s_ui.car_limit_ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_ui.car_limit_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_ui.car_limit_ring, 182, 182);
    lv_arc_set_rotation(s_ui.car_limit_ring, 270);
    lv_arc_set_bg_angles(s_ui.car_limit_ring, 0, 360);
    lv_arc_set_range(s_ui.car_limit_ring, 0, 100);
    lv_arc_set_value(s_ui.car_limit_ring, 0);
    lv_obj_set_style_arc_width(s_ui.car_limit_ring, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui.car_limit_ring, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui.car_limit_ring, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui.car_limit_ring, lv_color_hex(0x2288FF), LV_PART_INDICATOR);
    lv_obj_align(s_ui.car_limit_ring, LV_ALIGN_CENTER, 0, -16);
    lv_obj_add_flag(s_ui.car_limit_ring, LV_OBJ_FLAG_HIDDEN);

    // Bien bao gioi han la trong tam (to, de nhin nhat) - kieu chau Au: hinh
    // tron nen trang, vien do day, so den lon. Toc do hien tai chi la chu
    // nho ben duoi, mang tinh tham khao.
    s_ui.car_limit_sign = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.car_limit_sign);
    lv_obj_set_size(s_ui.car_limit_sign, 150, 150);
    lv_obj_set_style_radius(s_ui.car_limit_sign, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.car_limit_sign, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_ui.car_limit_sign, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ui.car_limit_sign, lv_color_hex(0xE02020), 0);
    lv_obj_set_style_border_width(s_ui.car_limit_sign, 8, 0);
    lv_obj_align(s_ui.car_limit_sign, LV_ALIGN_CENTER, 0, -16);
    lv_obj_add_flag(s_ui.car_limit_sign, LV_OBJ_FLAG_HIDDEN);

    s_ui.car_limit_label = lv_label_create(s_ui.car_limit_sign);
    lv_obj_set_style_text_color(s_ui.car_limit_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_ui.car_limit_label, &lv_font_speed_64, 0);
    lv_obj_center(s_ui.car_limit_label);
    lv_label_set_text(s_ui.car_limit_label, "0");

    s_ui.car_speed_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.car_speed_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_ui.car_speed_label, &lv_font_vi_20, 0);
    lv_obj_align(s_ui.car_speed_label, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_label_set_text(s_ui.car_speed_label, "0 km/h");
    lv_obj_add_flag(s_ui.car_speed_label, LV_OBJ_FLAG_HIDDEN);

    ui_show_connecting();

    lvgl_port_unlock();
}

void ui_show_provisioning(const char *ap_ssid, const char *ap_password, bool wifi_lost)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_ui.status_bar_label, "Setup");
    set_icon(LV_SYMBOL_WIFI, lv_color_hex(0xFFAA00));
    char subtitle[144];
    snprintf(subtitle, sizeof(subtitle), "WiFi: %s\nMật khẩu: %s\nMở trình duyệt bất kỳ trang để cài đặt",
             ap_ssid, ap_password);
    set_texts(wifi_lost ? "Mất kết nối WiFi" : "Cần cấu hình WiFi", subtitle);
    clear_transcripts_locked();
    lvgl_port_unlock();
}

void ui_show_connecting(void)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_ui.status_bar_label, "...");
    set_icon(LV_SYMBOL_REFRESH, lv_color_hex(0xFFAA00));
    set_texts("Đang kết nối WiFi", "");
    clear_transcripts_locked();
    lvgl_port_unlock();
}

void ui_show_idle(void)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_ui.status_bar_label, "WiFi");
    set_icon(LV_SYMBOL_CALL, lv_color_hex(0x33CC66));
    set_texts("Sẵn sàng", "Nhấn nút để nói chuyện");
    clear_transcripts_locked();
    lvgl_port_unlock();
}

void ui_show_listening(void)
{
    lvgl_port_lock(0);
    set_icon(LV_SYMBOL_CALL, lv_color_hex(0xFF3355));
    set_texts("Đang nghe...", "Nhấn nút để kết thúc");
    lvgl_port_unlock();
}

void ui_show_thinking(void)
{
    lvgl_port_lock(0);
    set_icon(LV_SYMBOL_LOOP, lv_color_hex(0xCC88FF));
    set_texts("Đang xử lý...", "");
    lvgl_port_unlock();
}

void ui_show_speaking(void)
{
    lvgl_port_lock(0);
    set_icon(LV_SYMBOL_VOLUME_MAX, lv_color_hex(0x3399FF));
    set_texts("Đang trả lời...", "");
    lvgl_port_unlock();
}

void ui_show_error(const char *message)
{
    lvgl_port_lock(0);
    set_icon(LV_SYMBOL_WARNING, lv_color_hex(0xCC0000));
    set_texts("Lỗi", message ? message : "");
    clear_transcripts_locked();
    lvgl_port_unlock();
}

void ui_show_volume_overlay(uint8_t percent)
{
    lvgl_port_lock(0);
    const char *icon = percent == 0 ? LV_SYMBOL_MUTE : (percent < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d%%", icon, percent);
    lv_label_set_text(s_ui.volume_overlay, buf);
    lv_obj_clear_flag(s_ui.volume_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_t *timer = lv_timer_create(volume_overlay_hide_cb, 1200, NULL);
    lv_timer_set_repeat_count(timer, 1);
    lvgl_port_unlock();
}

void ui_update_battery(uint8_t percent, bool charging)
{
    lvgl_port_lock(0);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%d%% %s", charging ? LV_SYMBOL_CHARGE : "", percent, LV_SYMBOL_BATTERY_FULL);
    lv_label_set_text(s_ui.battery_label, buf);
    lvgl_port_unlock();
}

void ui_set_user_text(const char *text)
{
    lvgl_port_lock(0);
    char buf[256];
    snprintf(buf, sizeof(buf), "Bạn: %s", text ? text : "");
    lv_label_set_text(s_ui.user_text_label, buf);
    lvgl_port_unlock();
}

void ui_set_ai_text(const char *text)
{
    lvgl_port_lock(0);
    char buf[512];
    snprintf(buf, sizeof(buf), "AI: %s", text ? text : "");
    lv_label_set_text(s_ui.ai_text_label, buf);
    lvgl_port_unlock();
}

void ui_clear_transcripts(void)
{
    lvgl_port_lock(0);
    clear_transcripts_locked();
    lvgl_port_unlock();
}

void ui_show_car_mode(void)
{
    lvgl_port_lock(0);
    set_assistant_widgets_visible(false);
    lv_obj_clear_flag(s_ui.car_speed_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.car_limit_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.car_limit_sign, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    ui_set_ble_connected(false); // vua bat BLE, chua co dien thoai nao ket noi
}

void ui_set_ble_connected(bool connected)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_ui.status_bar_label, connected ? "Da ket noi" : "Mat ket noi");
    lv_obj_set_style_text_color(s_ui.status_bar_label, connected ? lv_color_hex(0x33CC66) : lv_color_hex(0x999999), 0);
    lvgl_port_unlock();
}

void ui_car_update(uint16_t speed_kmh, uint16_t limit_kmh)
{
    lvgl_port_lock(0);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u km/h", (unsigned)speed_kmh);
    lv_label_set_text(s_ui.car_speed_label, buf);
    snprintf(buf, sizeof(buf), "%u", (unsigned)limit_kmh);
    lv_label_set_text(s_ui.car_limit_label, buf);

    int percent = (limit_kmh > 0) ? ((int)speed_kmh * 100 / (int)limit_kmh) : 0;
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    lv_arc_set_value(s_ui.car_limit_ring, percent);
    lvgl_port_unlock();
}

void ui_flash_over_limit(void)
{
    lvgl_port_lock(0);
    if (s_bg_flash_timer) {
        lv_timer_del(s_bg_flash_timer);
    }
    s_bg_flash_step = 0;
    s_bg_flash_timer = lv_timer_create(bg_flash_cb, BG_FLASH_PERIOD_MS, NULL);
    lvgl_port_unlock();
}

void ui_flash_limit_changed(void)
{
    lvgl_port_lock(0);
    if (s_limit_blink_timer) {
        lv_timer_del(s_limit_blink_timer);
    }
    s_limit_blink_step = 0;
    s_limit_blink_timer = lv_timer_create(limit_blink_cb, LIMIT_BLINK_PERIOD_MS, NULL);
    lvgl_port_unlock();
}
