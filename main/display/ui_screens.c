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

typedef struct {
    lv_obj_t *status_bar_label;   // goc tren trai: trang thai wifi/AP
    lv_obj_t *battery_label;      // goc tren phai: % pin
    lv_obj_t *icon_label;         // icon LV_SYMBOL_*, mau doi theo state
    lv_obj_t *title_label;        // ten trang thai ngan, chu lon
    lv_obj_t *subtitle_label;     // huong dan / thong bao loi / thong tin provisioning
    lv_obj_t *text_container;     // khung chua 2 dong transcript, xep flex-column
    lv_obj_t *user_text_label;    // "Ban: ..." transcript nguoi dung (STT)
    lv_obj_t *ai_text_label;      // "AI: ..." transcript Gemini tra loi
    lv_obj_t *volume_overlay;     // icon+% volume, an/hien tam thoi
} ui_widgets_t;

static ui_widgets_t s_ui;

static void set_icon(const char *symbol, lv_color_t color)
{
    lv_label_set_text(s_ui.icon_label, symbol);
    lv_obj_set_style_text_color(s_ui.icon_label, color, 0);
}

static void set_texts(const char *title, const char *subtitle)
{
    lv_label_set_text(s_ui.title_label, title);
    lv_label_set_text(s_ui.subtitle_label, subtitle);
}

static void volume_overlay_hide_cb(lv_timer_t *timer)
{
    lv_obj_add_flag(s_ui.volume_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_del(timer);
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
